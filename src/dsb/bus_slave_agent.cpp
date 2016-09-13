#include "dsb/bus/slave_agent.hpp"

#include <cassert>
#include <limits>
#include <utility>

#include "dsb/error.hpp"
#include "dsb/log.hpp"
#include "dsb/net/zmqx.hpp"
#include "dsb/protobuf.hpp"
#include "dsb/protocol/execution.hpp"
#include "dsb/protocol/glue.hpp"
#include "dsb/slave/exception.hpp"
#include "dsb/util.hpp"


namespace
{
    uint16_t NormalMessageType(const std::vector<zmq::message_t>& msg)
    {
        const auto mt = dsb::protocol::execution::NonErrorMessageType(msg);
        DSB_LOG_TRACE(boost::format("Received %s")
            % dsbproto::execution::MessageType_Name(
                static_cast<dsbproto::execution::MessageType>(mt)));
        if (mt == dsbproto::execution::MSG_TERMINATE) throw dsb::bus::Shutdown();
        return mt;
    }

    void InvalidReplyFromMaster()
    {
        throw dsb::error::ProtocolViolationException("Invalid reply from master");
    }

    void EnforceMessageType(
        const std::vector<zmq::message_t>& msg,
        dsbproto::execution::MessageType expectedType)
    {
        if (NormalMessageType(msg) != expectedType) InvalidReplyFromMaster();
    }

    const size_t DATA_HEADER_SIZE = 4;
}


namespace dsb
{
namespace bus
{


SlaveAgent::SlaveAgent(
    dsb::net::Reactor& reactor,
    dsb::slave::Instance& slaveInstance,
    const dsb::net::Endpoint& controlEndpoint,
    const dsb::net::Endpoint& dataPubEndpoint,
    std::chrono::milliseconds commTimeout)
    : m_stateHandler(&SlaveAgent::NotConnectedHandler),
      m_slaveInstance(slaveInstance),
      m_commTimeout(commTimeout),
      m_id(dsb::model::INVALID_SLAVE_ID),
      m_currentStepID(dsb::model::INVALID_STEP_ID)
{
    m_control.Bind(controlEndpoint);
    DSB_LOG_TRACE("Slave bound to control endpoint: " + BoundControlEndpoint().URL());

    m_publisher.Bind(dataPubEndpoint);
    DSB_LOG_TRACE("Slave bound to data publisher endpoint: " + BoundControlEndpoint().URL());

    reactor.AddSocket(
        m_control.Socket(),
        [this](dsb::net::Reactor& r, zmq::socket_t& s) {
            assert(&s == &m_control.Socket());
            std::vector<zmq::message_t> msg;
            if (!dsb::net::zmqx::Receive(m_control, msg, m_commTimeout)) {
                throw dsb::slave::TimeoutException(
                    std::chrono::duration_cast<std::chrono::seconds>(m_commTimeout));
            }
            try {
                RequestReply(msg);
            } catch (const dsb::bus::Shutdown&) {
                // TODO: Handle slave shutdown via other means?
                r.Stop();
                return;
            }
#ifdef DSB_LOG_TRACE_ENABLED
            const auto replyType = static_cast<dsbproto::execution::MessageType>(
                dsb::protocol::execution::ParseMessageType(msg.front()));
#endif
            m_control.Send(msg);
            DSB_LOG_TRACE(boost::format("Sent %s")
                % dsbproto::execution::MessageType_Name(replyType));
        });
}


dsb::net::Endpoint SlaveAgent::BoundControlEndpoint() const
{
    return dsb::net::Endpoint{m_control.BoundEndpoint().URL()};
}


dsb::net::Endpoint SlaveAgent::BoundDataPubEndpoint() const
{
    return m_publisher.BoundEndpoint();
}


void SlaveAgent::RequestReply(std::vector<zmq::message_t>& msg)
{
    (this->*m_stateHandler)(msg);
}


void SlaveAgent::NotConnectedHandler(std::vector<zmq::message_t>& msg)
{
    DSB_LOG_TRACE("NOT CONNECTED state: incoming message");
    if (dsb::protocol::execution::ParseHelloMessage(msg) != 0) {
        throw std::runtime_error("Master required unsupported protocol");
    }
    DSB_LOG_TRACE("Received HELLO");
    dsb::protocol::execution::CreateHelloMessage(msg, 0);
    m_stateHandler = &SlaveAgent::ConnectedHandler;
}


void SlaveAgent::ConnectedHandler(std::vector<zmq::message_t>& msg)
{
    DSB_LOG_TRACE("CONNECTED state: incoming message");
    EnforceMessageType(msg, dsbproto::execution::MSG_SETUP);
    if (msg.size() != 2) InvalidReplyFromMaster();

    dsbproto::execution::SetupData data;
    dsb::protobuf::ParseFromFrame(msg[1], data);
    DSB_LOG_DEBUG(boost::format("Slave name (ID): %s (%d)")
        % data.slave_name() % data.slave_id());
    DSB_LOG_DEBUG(boost::format("Simulation time frame: %g to %g")
        % data.start_time()
        % (data.has_stop_time() ? data.stop_time() : std::numeric_limits<double>::infinity()));
    m_id = data.slave_id();
    m_slaveInstance.Setup(
        data.start_time(),
        data.has_stop_time() ? data.stop_time() : std::numeric_limits<double>::infinity(),
        data.execution_name(),
        data.slave_name());

    dsb::protocol::execution::CreateMessage(msg, dsbproto::execution::MSG_READY);
    m_stateHandler = &SlaveAgent::ReadyHandler;
}


void SlaveAgent::ReadyHandler(std::vector<zmq::message_t>& msg)
{
    DSB_LOG_TRACE("READY state: incoming message");
    switch (NormalMessageType(msg)) {
        case dsbproto::execution::MSG_STEP: {
            if (msg.size() != 2) {
                throw dsb::error::ProtocolViolationException(
                    "Wrong number of frames in STEP message");
            }
            dsbproto::execution::StepData stepData;
            dsb::protobuf::ParseFromFrame(msg[1], stepData);
            if (Step(stepData)) {
                dsb::protocol::execution::CreateMessage(msg, dsbproto::execution::MSG_STEP_OK);
                m_stateHandler = &SlaveAgent::PublishedHandler;
            } else {
                dsb::protocol::execution::CreateMessage(msg, dsbproto::execution::MSG_STEP_FAILED);
                m_stateHandler = &SlaveAgent::StepFailedHandler;
            }
            break; }
        case dsbproto::execution::MSG_SET_VARS:
            HandleSetVars(msg);
            break;
        case dsbproto::execution::MSG_SET_PEERS:
            HandleSetPeers(msg);
            break;
        case dsbproto::execution::MSG_DESCRIBE:
            HandleDescribe(msg);
            break;
        default:
            InvalidReplyFromMaster();
    }
}


void SlaveAgent::PublishedHandler(std::vector<zmq::message_t>& msg)
{
    DSB_LOG_TRACE("STEP OK state: incoming message");
    EnforceMessageType(msg, dsbproto::execution::MSG_ACCEPT_STEP);
    // TODO: Use a different timeout here?
    m_connections.Update(m_slaveInstance, m_currentStepID, m_commTimeout);
    dsb::protocol::execution::CreateMessage(msg, dsbproto::execution::MSG_READY);
    m_stateHandler = &SlaveAgent::ReadyHandler;
}


void SlaveAgent::StepFailedHandler(std::vector<zmq::message_t>& msg)
{
    DSB_LOG_TRACE("STEP FAILED state: incoming message");
    EnforceMessageType(msg, dsbproto::execution::MSG_TERMINATE);
    // We never get here, because EnforceMessageType() always throws either
    // Shutdown or ProtocolViolationException.
    assert (false);
}


void SlaveAgent::HandleDescribe(std::vector<zmq::message_t>& msg)
{
    dsbproto::execution::SlaveDescription sd;
    *sd.mutable_type_description() =
        dsb::protocol::ToProto(m_slaveInstance.TypeDescription());
    dsb::protocol::execution::CreateMessage(
        msg, dsbproto::execution::MSG_READY, sd);
}


namespace
{
    class SetVariable : public boost::static_visitor<>
    {
    public:
        SetVariable(
            dsb::slave::Instance& slaveInstance,
            dsb::model::VariableID varRef)
            : m_slaveInstance(slaveInstance), m_varRef(varRef) { }
        void operator()(double value) const
        {
            m_slaveInstance.SetRealVariable(m_varRef, value);
        }
        void operator()(int value) const
        {
            m_slaveInstance.SetIntegerVariable(m_varRef, value);
        }
        void operator()(bool value) const
        {
            m_slaveInstance.SetBooleanVariable(m_varRef, value);
        }
        void operator()(const std::string& value) const
        {
            m_slaveInstance.SetStringVariable(m_varRef, value);
        }
    private:
        dsb::slave::Instance& m_slaveInstance;
        dsb::model::VariableID m_varRef;
    };
}


// TODO: Make this function signature more consistent with Step() (or the other
// way around).
void SlaveAgent::HandleSetVars(std::vector<zmq::message_t>& msg)
{
    if (msg.size() != 2) {
        throw dsb::error::ProtocolViolationException(
            "Wrong number of frames in SET_VARS message");
    }
    DSB_LOG_DEBUG("Setting/connecting variables");
    dsbproto::execution::SetVarsData data;
    dsb::protobuf::ParseFromFrame(msg[1], data);
    for (const auto& varSetting : data.variable()) {
        // TODO: Catch and report errors
        if (varSetting.has_value()) {
            const auto val = dsb::protocol::FromProto(varSetting.value());
            boost::apply_visitor(
                SetVariable(m_slaveInstance, varSetting.variable_id()),
                val);
        }
        if (varSetting.has_connected_output()) {
            m_connections.Couple(
                dsb::protocol::FromProto(varSetting.connected_output()),
                varSetting.variable_id());
        }
    }
    DSB_LOG_TRACE("Done setting/connecting variables");
    dsb::protocol::execution::CreateMessage(msg, dsbproto::execution::MSG_READY);
}


void SlaveAgent::HandleSetPeers(std::vector<zmq::message_t>& msg)
{
    if (msg.size() != 2) {
        throw dsb::error::ProtocolViolationException(
            "Wrong number of frames in SET_PEERS message");
    }
    DSB_LOG_DEBUG("Reconnecting to peers");
    dsbproto::execution::SetPeersData data;
    dsb::protobuf::ParseFromFrame(msg[1], data);
    std::vector<dsb::net::Endpoint> m_endpoints;
    for (const auto& peer : data.peer()) {
        m_endpoints.emplace_back(peer);
    }
    m_connections.Connect(m_endpoints.data(), m_endpoints.size());
    DSB_LOG_TRACE("Done reconnecting to peers");
    dsb::protocol::execution::CreateMessage(msg, dsbproto::execution::MSG_READY);
}


namespace
{
    dsb::model::ScalarValue GetVariable(
        const dsb::slave::Instance& slave,
        const dsb::model::VariableDescription& variable)
    {
        switch (variable.DataType()) {
            case dsb::model::REAL_DATATYPE:
                return slave.GetRealVariable(variable.ID());
            case dsb::model::INTEGER_DATATYPE:
                return slave.GetIntegerVariable(variable.ID());
            case dsb::model::BOOLEAN_DATATYPE:
                return slave.GetBooleanVariable(variable.ID());
            case dsb::model::STRING_DATATYPE:
                return slave.GetStringVariable(variable.ID());
            default:
                assert (!"Variable has unknown data type");
                return dsb::model::ScalarValue();
        }
    }
}


bool SlaveAgent::Step(const dsbproto::execution::StepData& stepInfo)
{
    m_currentStepID = stepInfo.step_id();
    if (!m_slaveInstance.DoStep(stepInfo.timepoint(), stepInfo.stepsize())) {
        return false;
    }
    for (const auto& varInfo : m_slaveInstance.TypeDescription().Variables()) {
        if (varInfo.Causality() != dsb::model::OUTPUT_CAUSALITY) continue;
        m_publisher.Publish(
            m_currentStepID,
            m_id,
            varInfo.ID(),
            GetVariable(m_slaveInstance, varInfo));
    }
    return true;
}


// =============================================================================
// class SlaveAgent::Connections
// =============================================================================

void SlaveAgent::Connections::Connect(
    const dsb::net::Endpoint* endpoints,
    std::size_t endpointsSize)
{
    m_subscriber.Connect(endpoints, endpointsSize);
}


void SlaveAgent::Connections::Couple(
    dsb::model::Variable remoteOutput,
    dsb::model::VariableID localInput)
{
    Decouple(localInput);
    if (!remoteOutput.Empty()) {
        m_subscriber.Subscribe(remoteOutput);
        m_connections.insert(ConnectionBimap::value_type(remoteOutput, localInput));
    }
}


void SlaveAgent::Connections::Update(
    dsb::slave::Instance& slaveInstance,
    dsb::model::StepID stepID,
    std::chrono::milliseconds timeout)
{
    m_subscriber.Update(stepID, timeout);
    for (const auto& conn : m_connections.left) {
        boost::apply_visitor(
            SetVariable(slaveInstance, conn.second),
            m_subscriber.Value(conn.first));
    }
}


void SlaveAgent::Connections::Decouple(dsb::model::VariableID localInput)
{
    const auto conn = m_connections.right.find(localInput);
    if (conn == m_connections.right.end()) return;
    const auto remoteOutput = conn->second;
    m_connections.right.erase(conn);
    if (m_connections.left.count(remoteOutput) == 0) {
        m_subscriber.Unsubscribe(remoteOutput);
    }
    assert(m_connections.right.count(localInput) == 0);
}


}} // namespace