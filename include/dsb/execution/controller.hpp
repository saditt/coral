/**
\file
\brief Functionality for starting and controlling an execution.
*/
#ifndef DSB_EXECUTION_CONTROLLER_HPP
#define DSB_EXECUTION_CONTROLLER_HPP

#include <memory>
#include <string>
#include "zmq.hpp"

#include "dsb/domain/locator.hpp"
#include "dsb/execution/locator.hpp"
#include "dsb/model.hpp"
#include "dsb/sequence.hpp"


namespace dsb
{
namespace execution
{


/**
\brief  Master execution controller.

This class represents the master entity in an execution, and is used to
initialize, run and shut down the simulation.

Objects of this class should not be created directly; use SpawnExecution()
instead.  The class is movable, but not copyable, thus ensuring that exactly
one Controller object is attached to an execution at any time.
*/
// TODO: Decide what happens when the Controller object goes out of scope!
class Controller
{
public:
    // Constructor.  (Explicitly not Doxygen-documented, since it should
    // only be called by SpawnExecution().
    Controller(const dsb::execution::Locator& locator);

    /// Move constructor.
    Controller(Controller&& other);

    /// Move assignment operator.
    Controller& operator=(Controller&& other);

    /**
    \brief  Sets the start time and, optionally, the stop time of the
            simulation.

    This function must be called before any AddSlave() calls are made.  The
    reason is that this information must be transmitted to the slaves as they
    connect and initialize, to allow them to report whether their model is
    valid within the given boundaries and/or to allocate memory for their
    internal state.

    If this function is never called, a default start time of 0.0 and an
    undefined stop time is used.

    \param [in] startTime   The start time of the simulation.
    \param [in] stopTime    The stop time of the simulation.  This may be
                            dsb::model::ETERNITY (the default), signifying that
                            no particular stop time is defined.

    \throws std::logic_error if `startTime > stopTime` or AddSlave() has
        previously been called.
    */
    void SetSimulationTime(
        dsb::model::TimePoint startTime,
        dsb::model::TimePoint stopTime = dsb::model::ETERNITY);

    /**
    \brief  Adds a slave to the execution.

    This function must be called in order to allow the slave to connect to
    the execution.  Any slave which attempts to connect before it has been
    added will receive a DENIED message immediately upon connection.

    \param [in] slaveId     The ID of the slave.

    \throws std::logic_error if the slave has been added before.
    */
    void AddSlave(dsb::model::SlaveID slaveId);

    /**
    \brief  Sets the values of some of a slave's variables.

    \param [in] slaveId     The ID of a slave which is part of the execution.
    \param [in] variables   Variable references and values.

    \throws std::logic_error if the ID does not correspond to a slave which
        is part of this execution.
    */
    void SetVariables(
        dsb::model::SlaveID slaveId,
        dsb::sequence::Sequence<dsb::model::VariableValue> variables);

    /**
    \brief  Connects inputs of one slave to outputs of other slaves.

    \param [in] slaveId     The ID of the slave whose inputs are to be connected.
    \param [in] connections References to input and output variables.

    \throws std::logic_error if `slaveId` or any of the slave IDs in
        `connections` do not refer to slaves which have been added with
        AddSlave().
    */
    void ConnectVariables(
        dsb::model::SlaveID slaveId,
        dsb::sequence::Sequence<dsb::model::VariableConnection> connections);

    /**
    \brief  Performs a time step.

    \param [in] t   The current time point.
    \param [in] dt  The step size.
    */
    void Step(dsb::model::TimePoint t, dsb::model::TimeDuration dt);

    /**
    \brief  Terminates the execution.

    This function will tell all participants to terminate, and then return
    immediately.  It does not (and can not) verify that the participants do
    in fact terminate.  Once all participants have been notified, the execution
    itself, and the thread it is running in, will terminate.

    No other methods may be called after a successful Terminate() call.
    */
    void Terminate();

    //TODO:
    //void Terminate(uint16_t slaveId);
    //SimState StoreSimulationState();
    //void RestoreSimulationState(const SimState& state);

private:
    // Waits until the execution is in the READY state.
    // This function is private for the time being, but it is likely to be made
    // public at some later time.
    void WaitForReady();

    std::shared_ptr<zmq::context_t> m_context;
    zmq::socket_t m_rpcSocket;
    zmq::socket_t m_asyncInfoSocket;
};


/**
\brief  Spawns a new execution.

This will start a new execution in a background thread, and return an object
which may be used to control the execution.

\param [in] context     The ZMQ context to use for all communication, both
                        internal and external.
\param [in] endpoint    The address of the execution broker.
*/
dsb::execution::Locator SpawnExecution(const dsb::domain::Locator& domainLocator);


}}      //namespace
#endif  // header guard