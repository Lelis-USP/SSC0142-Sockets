#include <iostream>
#include "config.h"

namespace config {
    ConnectionConfig parse_config(int argc, char *argv[]) {
        ConnectionConfig config = {DEFAULT_HOST, DEFAULT_PORT};

        // Parse port from the CLI args
        if (argc > 1) {
            int port_raw = std::atoi(argv[1]);
            if (port_raw < 0 || port_raw > 65535) {
                std::cerr << "Error: port out of bounds" << std::endl;
                std::exit(1);
            }

            config.port = (uint16_t) port_raw;
        }

        // Parse host from the CLI args
        if (argc > 2) {
            std::string host = argv[2];
            config.host = host;
        }

        return config;
    }
}
