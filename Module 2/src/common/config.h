#pragma once

#include <string>
#include <chrono>
#include <stdexcept>
#include <cinttypes>
#include <limits>

namespace config {
    static const uint16_t MAX_MESSAGE_SIZE = 4096;

    static const std::string DEFAULT_HOST = "127.0.0.1";
    static const std::uint16_t DEFAULT_PORT = 60332;

    // 1s listen timeout
    static const struct timeval TCP_RECEIVE_TIMEOUT = {
            .tv_sec = 1,
            .tv_usec = 0,
    };

    // 1s send timeout
    static const struct timeval TCP_SEND_TIMEOUT = {
            .tv_sec = 1,
            .tv_usec = 0,
    };

    // Polling interval of 0.1ms
    static const std::chrono::duration POLLING_INTERVAL = std::chrono::microseconds(100);

    // Max send tries
    static const int MAX_SEND_TRIES = 5;

    struct ConnectionConfig {
        std::string host;
        std::uint16_t port;
        bool blocking;
    };

    ConnectionConfig parse_config(int argc, char *argv[]);
}
