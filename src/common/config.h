#pragma once

#include <string>
#include <stdexcept>
#include <cinttypes>
#include <limits>

namespace config {
    static const uint16_t MAX_MESSAGE_SIZE = 4096;

    static const std::string DEFAULT_HOST = "127.0.0.1";
    static const std::uint16_t DEFAULT_PORT = 60332;

    struct ConnectionConfig {
        std::string host;
        std::uint16_t port;
    };

    ConnectionConfig parse_config(int argc, char* argv[]);
}
