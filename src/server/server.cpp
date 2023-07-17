#include<iostream>
#include<thread>
#include<csignal>

#include "../common/config.h"
#include "../common/error.h"
#include "../common/network.h"

#include "worker.h"

static worker::server::State state = {
        .socket_fd = -1,
        .kill = false,
};

// Handle signals
static void sig_handler(int sig) {
    std::cout << "\rInterrupting server..." << std::endl;

    // Trigger kill
    state.kill = true;
}

int main(int argc, char *argv[]) {
    // Register signal handler
    if (std::signal(SIGINT, sig_handler) == SIG_ERR) {
        error::error("Failed to register signal handler");
        return 1;
    }

    // Load host & port config from the command line
    config::ConnectionConfig config = config::parse_config(argc, argv);

    // Spin-up new server using the given configuration
    int socket_fd = network::listen(config);
    state.socket_fd = socket_fd;

    if (socket_fd == -1) {
        std::cout << "Failed to bind address" << std::endl;
        return 1;
    }

    // Initiate manager thread and join it
    std::thread manager_thread(worker::server::manager, &state);
    manager_thread.join();

    // Release socket
    if (state.socket_fd != -1) {
        close(state.socket_fd);
    }

    std::cout << "\r\nServer interrupted" << std::endl;

    return 0;
}
