#pragma once

#include<atomic>
#include<memory>
#include<mutex>
#include<queue>
#include<string>
#include<unordered_map>

#include<unistd.h>

#include "../common/network.h"

namespace worker::server {
    struct Client {
        network::Connection connection; // Client connection with socket fd
        std::string ip_str; // Parsed client IP (for convenience)
        std::string nickname; // Client assigned nickname
        std::atomic<bool> alive; // Should the collection be alive

        std::mutex message_queue_mutex;
        std::queue<std::shared_ptr<std::string> > message_queue;
    };

    struct State {
        int socket_fd; // Listening socket fd
        std::atomic<bool> kill;

        std::mutex clients_mutex;
        std::unordered_map<std::string, std::shared_ptr<Client> > clients;
    };

    void manager(State *state);

    void communicator(std::shared_ptr<Client> client_ptr, State *state);

    bool communicator_outgoing(std::shared_ptr<Client> client_ptr, State *state);

    bool communicator_incoming(std::shared_ptr<Client> client_ptr, State *state, char buffer[]);

    bool try_send_message(const std::string &message, std::pair<const std::shared_ptr<Client>, int> &client_info);

    void handle_message(const std::string &message, std::shared_ptr<Client> client_ptr, State *state);

    void broadcast_message(const std::string &message, State *state);
}