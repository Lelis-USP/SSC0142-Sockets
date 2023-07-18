#pragma once

#include<vector>

#include<unistd.h>
#include <atomic>
#include <mutex>
#include <queue>

#include "../common/network.h"

namespace worker::client {
    struct State {
        int socket_fd;
        config::ConnectionConfig config;
        std::atomic<bool> kill;

        std::mutex message_queue_mutex;
        std::queue<std::string> pending_messages;
    };

    void manager(State *state);

    void input(State *state);

    void communicator(State *state);

    bool communicator_outgoing(State *state);

    bool communicator_incoming(State *state, char buffer[]);

    void handle_message(std::string message, State *state);

    void handle_connect(State *state);

    void handle_quit(State *state);
}