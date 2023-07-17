#include<iostream>
#include<thread>
#include<csignal>

#include "../common/config.h"
#include "../common/error.h"

#include "worker.h"

static worker::client::State state = {
        .socket_fd = -1,
        .kill = false,
};

// Handle signals
static void sig_handler(int sig) {
    std::cout << "\rInterrupting client..." << std::endl;
    state.kill = true;
}

// Connection Handler

int main(int argc, char *argv[]) {
    // Register signal handler
    if (std::signal(SIGINT, sig_handler) == SIG_ERR) {
        error::error("Failed to register signal handler");
        return 1;
    }

    // Load host & port config from the command line
    state.config = config::parse_config(argc, argv);

    // Initiate manager thread and join it
    std::thread manager_thread(worker::client::manager, &state);
    manager_thread.join();

    std::cout << "\r\nClient interrupted" << std::endl;

    return 0;
}
