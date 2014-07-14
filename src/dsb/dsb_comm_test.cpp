#include <cstring>
#include "gtest/gtest.h"
#include "dsb/comm.hpp"

using namespace dsb::comm;


TEST(dsb_comm, SendReceiveMessage)
{
    auto ctx = zmq::context_t();
    auto sender = zmq::socket_t(ctx, ZMQ_PUSH);
    auto recver = zmq::socket_t(ctx, ZMQ_PULL);
    const auto endpoint = std::string("inproc://")
        + ::testing::UnitTest::GetInstance()->current_test_info()->test_case_name();
    recver.bind(endpoint.c_str());
    sender.connect(endpoint.c_str());

    std::deque<zmq::message_t> srcMsg;
    srcMsg.push_back(zmq::message_t(123));
    srcMsg.push_back(zmq::message_t());
    srcMsg.push_back(zmq::message_t(321));
    Send(sender, srcMsg);

    std::deque<zmq::message_t> tgtMsg(1);
    Receive(recver, tgtMsg);
    ASSERT_EQ(  3U, tgtMsg.size());
    EXPECT_EQ(123U, tgtMsg[0].size());
    EXPECT_EQ(  0U, tgtMsg[1].size());
    EXPECT_EQ(321U, tgtMsg[2].size());
}

TEST(dsb_comm, SendReceiveAddressedMessage)
{
    auto ctx = zmq::context_t();
    auto sender = zmq::socket_t(ctx, ZMQ_PUSH);
    auto recver = zmq::socket_t(ctx, ZMQ_PULL);
    const auto endpoint = std::string("inproc://")
        + ::testing::UnitTest::GetInstance()->current_test_info()->test_case_name();
    recver.bind(endpoint.c_str());
    sender.connect(endpoint.c_str());

    std::deque<zmq::message_t> env;
    env.push_back(zmq::message_t(3));
    std::memcpy(env.back().data(), "foo", 3);
    std::deque<zmq::message_t> srcMsg;
    srcMsg.push_back(zmq::message_t(123));
    srcMsg.push_back(zmq::message_t(321));
    AddressedSend(sender, env, srcMsg);

    std::deque<zmq::message_t> tgtMsg(1);
    Receive(recver, tgtMsg);
    ASSERT_EQ(4U, tgtMsg.size());
    ASSERT_EQ(3U, tgtMsg[0].size());
    EXPECT_EQ(0, std::memcmp(tgtMsg[0].data(), "foo", 3));
    EXPECT_EQ(  0U, tgtMsg[1].size());
    EXPECT_EQ(123U, tgtMsg[2].size());
    EXPECT_EQ(321U, tgtMsg[3].size());
}

TEST(dsb_comm, PopMessageEnvelope)
{
    std::deque<zmq::message_t> msg;
    msg.push_back(zmq::message_t(123));
    msg.push_back(zmq::message_t(321));
    msg.push_back(zmq::message_t());
    msg.push_back(zmq::message_t(97));
    std::deque<zmq::message_t> env;
    env.push_back(zmq::message_t());
    const auto size = PopMessageEnvelope(msg, &env);
    EXPECT_EQ(  3U, size);
    ASSERT_EQ(  2U, env.size());
    EXPECT_EQ(123U, env[0].size());
    EXPECT_EQ(321U, env[1].size());
    ASSERT_EQ(  1U, msg.size());
    EXPECT_EQ( 97U, msg[0].size());
}

TEST(dsb_comm, PopMessageEnvelope_emptyEnvelope)
{
    std::deque<zmq::message_t> msg;
    msg.push_back(zmq::message_t());
    msg.push_back(zmq::message_t(123));
    msg.push_back(zmq::message_t(321));
    msg.push_back(zmq::message_t(97));
    std::deque<zmq::message_t> env;
    env.push_back(zmq::message_t());
    const auto size = PopMessageEnvelope(msg, &env);
    EXPECT_EQ(  1U, size);
    EXPECT_EQ(  0U, env.size());
    ASSERT_EQ(  3U, msg.size());
    EXPECT_EQ(123U, msg[0].size());
    EXPECT_EQ(321U, msg[1].size());
    EXPECT_EQ( 97U, msg[2].size());
}

TEST(dsb_comm, PopMessageEnvelope_noEnvelope)
{
    std::deque<zmq::message_t> msg;
    msg.push_back(zmq::message_t(123));
    msg.push_back(zmq::message_t(321));
    msg.push_back(zmq::message_t(97));
    std::deque<zmq::message_t> env;
    env.push_back(zmq::message_t());
    const auto size = PopMessageEnvelope(msg, &env);
    EXPECT_EQ(  0U, size);
    EXPECT_EQ(  0U, env.size());
    ASSERT_EQ(  3U, msg.size());
    EXPECT_EQ(123U, msg[0].size());
    EXPECT_EQ(321U, msg[1].size());
    EXPECT_EQ( 97U, msg[2].size());
}

TEST(dsb_comm, PopMessageEnvelope_dropEnvelope)
{
    std::deque<zmq::message_t> msg;
    msg.push_back(zmq::message_t(123));
    msg.push_back(zmq::message_t(321));
    msg.push_back(zmq::message_t());
    msg.push_back(zmq::message_t(97));
    const auto size = PopMessageEnvelope(msg);
    EXPECT_EQ( 3U, size);
    ASSERT_EQ( 1U, msg.size());
    EXPECT_EQ(97U, msg[0].size());
}


TEST(dsb_comm, ToString)
{
    zmq::message_t msg(3);
    std::memcpy(msg.data(), "foo", 3);
    EXPECT_EQ("foo", ToString(msg));
}