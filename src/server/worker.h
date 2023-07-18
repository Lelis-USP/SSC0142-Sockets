#pragma once

#include<atomic>
#include<memory>
#include<mutex>
#include<queue>
#include<string>
#include<unordered_map>
#include<set>

#include<unistd.h>

#include "../common/network.h"

namespace worker::server {
    struct Client;
    struct Channel;
    struct State;

    struct Client {
        // Current client connection (socket and IP info)
        network::Connection connection;
        // Client IP string representation (for convenience)
        std::string ip_str;
        // Should the client connection still be alive
        std::atomic<bool> alive;

        // Clients' current nickname
        std::shared_ptr<std::string> nickname;

        // Message queue (messages that are pending to be sent to the given user)
        std::mutex message_queue_mutex;
        std::queue<std::shared_ptr<std::string> > message_queue;

        // Clients' currently joined channel
        std::shared_ptr<Channel> channel;

        void add_message(const std::shared_ptr<std::string> &message);
        std::shared_ptr<std::string> pop_message();
    };

    enum ChannelFlags {
        INVITE_ONLY = 1 << 0,
        SNAKES_ONLY = 1 << 15,
    };

    struct Channel {
        // Concurrency control for updating channel information
        std::mutex mutex;

        // Channel identifier
        std::string name;

        // Channel operator (user responsible for administrating the channel)
        std::shared_ptr<std::string> chop;
        // Channel configuration flags (control invite only mode and others)
        uint16_t flags;

        // Current members of the channel
        std::set<std::shared_ptr<Client> > members;
        // Users muted in the channel
        std::set<std::string> muted;
        // Users banned in the channel
        std::set<std::string> banned;
        // Invited users to the channel (for invite only mode)
        std::set<std::string> invites;
    };

    struct State {
        // Main listening socket file descriptor
        int socket_fd;

        // Server kill state flag
        std::atomic<bool> kill;

        // Clients which are "logged-in" (have a nickname configured)
        std::mutex registered_clients_mutex;
        std::unordered_map<std::string, std::shared_ptr<Client> > registered_clients;

        // Available clients on the server
        std::mutex clients_mutex;
        std::set<std::shared_ptr<Client>> clients;

        // Available channels on the server
        std::mutex channels_mutex;
        std::unordered_map<std::string, std::shared_ptr<Channel> > channels;
    };

    void manager(State *state);

    void communicator(const std::shared_ptr<Client>& client_ptr, State *state);

    bool communicator_outgoing(const std::shared_ptr<Client>& client_ptr, State *state);

    bool communicator_incoming(const std::shared_ptr<Client>& client_ptr, State *state, char buffer[]);

    bool try_send_message(const std::string &message, std::pair<const std::shared_ptr<Client>, int> &client_info);

    void handle(const std::string &message, const std::shared_ptr<Client>& client_ptr, State *state);

    void broadcast_message_channel(const std::string &message, const std::shared_ptr<Channel>& channel);
}