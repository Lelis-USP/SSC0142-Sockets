#include<iostream>
#include<thread>
#include<csignal>

#include<unistd.h>

#include "../common/config.h"
#include "../common/error.h"
#include "../common/network.h"
#include "../common/worker.h"

#include "worker.h"

static worker::server::State state = {
        .connection = {
                .socket_fd  = -1,
        },
        .socket_fd = -1,
        .manager_tid = 0,
        .listener_tid = 0,
};

// Handle signals
static void sig_handler(int sig) {
    std::cout << "\rInterrupting server..." << std::endl;

    if (state.connection.socket_fd != -1) {
        close(state.connection.socket_fd);
    }

    if (state.socket_fd != -1) {
        close(state.socket_fd);
    }

    if (state.listener_tid != 0) {
        pthread_cancel(state.listener_tid);
    }

    if (state.manager_tid != 0) {
        pthread_cancel(state.manager_tid);
    }

    std::cout << "Server interrupted" << std::endl;

    exit(1);
}

// Connection Handler

int main(int argc, char *argv[]) {
    // Register signal handler
    if (std::signal(SIGINT, sig_handler) == SIG_ERR) {
        error::critical_error("Failed to register signal handler");
        return 1;
    }

    // Load host & port config from the command line
    config::ConnectionConfig config = config::parse_config(argc, argv);

    // Spin-up new server using the given configuration
    int socket_fd = network::listen(config);

    state.socket_fd = socket_fd;

    if (worker::dispatch_thread(state.manager_tid, worker::server::manager, &state) != 0) {
        error::error("Failed to start connection manager!");
        return 1;
    }

    pthread_join(state.manager_tid, nullptr);

    return 0;
}
