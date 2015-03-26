/**
\file
\brief Misc. utility functions for communication-related purposes.
*/
#ifndef DSB_COMM_UTIL_HPP
#define DSB_COMM_UTIL_HPP

#include <cstdint>
#include <string>
#include "zmq.hpp"


namespace dsb
{
namespace comm
{


/**
\brief  Binds `socket` to an ephemeral TCP port on the given network interface
        and returns the port number.
*/
std::uint16_t BindToEphemeralPort(
    zmq::socket_t& socket,
    const std::string& networkInterface = "*");


/**
\brief Returns the value of the ZMQ_LAST_ENDPOINT socket property.
\throws zmq::error_t if ZMQ reports an error.
*/
std::string LastEndpoint(zmq::socket_t& socket);


/**
\brief  Given a string on the form "tcp://addr:port", returns the port number.

\throws std::invalid_argument if `endpoint` does not have the structure
    described above.
\throws std::bad_cast if the port number is not in a valid number format.
*/
std::uint16_t EndpointPort(const std::string& endpoint);


}}      // namespace
#endif  // header guard