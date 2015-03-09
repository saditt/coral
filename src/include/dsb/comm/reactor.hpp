/**
\file
\brief Contains the dsb::comm::Reactor class and related functionality.
*/
#ifndef DSB_COMM_REACTOR_HPP
#define DSB_COMM_REACTOR_HPP

#include <functional>
#include <vector>
#include <utility>

#include "boost/chrono/duration.hpp"
#include "boost/chrono/system_clocks.hpp"
#include "zmq.hpp"


namespace dsb
{
namespace comm
{


/**
\brief  An implementation of the reactor pattern.

This class polls a number of sockets, and when a socket has incoming messages,
it dispatches to the registered handler function(s) for that socket.
If multiple sockets have incoming messages, or there are multiple handlers for
one socket, the functions are called in the order they were added.

It also supports timed events, where a handler function is called a certain
number of times (or indefinitely) with a fixed time interval.  Timers are only
active when the messaging loop is running, i.e. between Run() and Stop().
*/
class Reactor
{
public:
    typedef boost::chrono::system_clock::time_point TimePoint;
    typedef std::function<void(Reactor&, zmq::socket_t&)> SocketHandler;
    typedef std::function<void(Reactor&, int)> TimerHandler;

    Reactor();

    /// Adds a handler for the given socket.
    void AddSocket(zmq::socket_t& socket, SocketHandler handler);

    /**
    \brief  Removes all handlers for the given socket.

    If this function is called by a socket handler, no more handlers will be
    called for the removed socket, even if the last poll indicated that it has
    incoming messages.
    */
    void RemoveSocket(zmq::socket_t& socket);

    /**
    \brief  Adds a timer.

    If the messaging loop is running, the first event will be triggered at
    `interval` after this function is called.  Otherwise, the first event will
    be triggered `interval` after Run() is called.

    \param [in] interval    The time between events.
    \param [in] count       The total number of events.  If negative, the timer
                            runs indefinitely.
    \param [in] handler     The event handler.

    \returns an index which may later be used to remove the timer.
    \throws std::invalid_argument if `count` is zero or `interval` is negative.
    */
    int AddTimer(
        boost::chrono::milliseconds interval,
        int count,
        TimerHandler handler);

    /**
    \brief  Removes a timer.
    \throws std::invalid_argument if `id` is not a valid timer ID.
    */
    void RemoveTimer(int id);

    /**
    \brief  Runs the messaging loop.

    This function does not return before Stop() is called (by one of the
    socket/timer handlers) or an error occurs.

    If a socket/timer handler throws an exception, the messaging loop will stop
    and the exception will propagate out of Run().

    \throws zmq::error_t if ZMQ reports an error.
    */
    void Run();

    /**
    \brief  Stops the messaging loop.

    This method may be called by a socket/timer handler, and will exit the
    messaging loop once all handlers for the current event(s) have been called.
    */
    void Stop();

private:
    void ResetTimers();
    boost::chrono::milliseconds TimeToNextEvent() const;
    void PerformNextEvent();

    // Rebuilds the list of poll items.
    void Rebuild();

    typedef std::pair<zmq::socket_t*, SocketHandler> SocketHandlerPair;
    std::vector<SocketHandlerPair> m_sockets;
    std::vector<zmq::pollitem_t> m_pollItems;

    int m_nextTimerID;
    struct Timer
    {
        int id;
        TimePoint nextEventTime;
        boost::chrono::milliseconds interval;
        int remaining;
        TimerHandler handler;
    };
    std::vector<Timer> m_timers;

    bool m_needsRebuild;
    bool m_continuePolling;
};


}}      // namespace
#endif  // header guard
