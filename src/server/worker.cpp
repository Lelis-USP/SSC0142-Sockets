#include<iomanip>
#include<iostream>
#include<map>
#include<mutex>
#include<memory>
#include<thread>

#include<strings.h>

#include "../common/error.h"

#include "worker.h"

namespace worker::server {

    ///////////////////////////
    // Connection Management //
    ///////////////////////////

    std::shared_ptr<Client> accept_client(State *state) {
        // Accept a new connection on the server listener socket. Since the call is non-blocking, it will likely result
        // in a EAGAIN or EWOULDBLOCK error, which is mapped as conn.socket_fd = -2. In those cases, we should retry
        // accepting a connection after a small delay.
        //
        // The implementation here ensures that a delay will only be triggered if there is no pending connections.
        network::Connection conn = network::accept_conn(state->socket_fd);

        // If there is no available pending connection, try again after a small delay
        while (conn.socket_fd == -2 && !state->kill) {
            // Delay the retry for the configured interval
            std::this_thread::sleep_for(config::POLLING_INTERVAL);
            // Retry to get a new connection
            conn = network::accept_conn(state->socket_fd);
        }

        // A kill signal was triggered, cleanup and exit
        if (state->kill) {
            // If for the unluckiest of odds, the application was killed at the same time a new connection was received
            // properly finish it
            if (conn.socket_fd >= 0) {
                close(conn.socket_fd);
            }

            return nullptr;
        }

        // There was an error while creation the connection, most likely irrecoverable
        // Log that an error happened and signal it (through the nullptr response) to the thread handler.
        if (conn.socket_fd == -1) {
            error::error("Connection failed!");
            return nullptr;
        }

        // Build new client using a shared pointer (easier to manage memory)
        std::shared_ptr<Client> client_ptr = std::make_shared<Client>();
        client_ptr->connection = conn; // Client's connection data, include the socket fd
        client_ptr->ip_str = network::address_repr(conn.client_address); // Parse the client IP into a string
        client_ptr->nickname = nullptr; // The client starts without an assigned nickname
        client_ptr->alive = true; // If the client is alive and happy :)

        // Return the shared pointer
        return client_ptr;
    }

    void manager(State *state) {
        // If no state, exit
        if (state == nullptr) {
            return;
        }

        // Client threads (for later joining)
        std::map<std::shared_ptr<worker::server::Client>, std::thread> threads;

        // While the application should be alive, accept new clients
        while (!state->kill) {

            // Try accepting a new client
            std::shared_ptr<Client> client_ptr = accept_client(state);

            // Check if a new client was successfully accepted
            if (!client_ptr) {
                // Most errors of this kind aren't recoverable, so we'll trigger a kill here
                state->kill = true;
                break;
            }

            std::cout << "New client from " << client_ptr->ip_str << std::endl;

            // We have a new connection, register it to the clients map. Here we do guarded access to the map to avoid
            // any concurrency issues. The guard is destroyed after leaving the context, releasing the lock.
            {
                auto guard = std::lock_guard<std::mutex>(state->clients_mutex);
                state->clients.insert(client_ptr);

                // Cleanup dead clients
                auto it = state->clients.begin();
                while (it != state->clients.end()) {
                    if ((*it)->alive) {
                        it++;
                        continue;
                    }

                    // Detach thread to avoid a thread leak
                    threads[(*it)].detach();
                    threads.erase((*it));

                    // Remove client from tracking list
                    it = state->clients.erase(it);
                }
            }

            // Add the thread to the tracking map
            threads.emplace(client_ptr, std::thread(communicator, client_ptr, state));
        }

        // The only we should reach here is if a kill signal was received, but let's ensure it anyway to prevent future
        // bugs.
        if (!state->kill) {
            state->kill = true;
        }

        // Wait for each communicator thread to die :)
        for (auto &entry: threads) {
            if (entry.second.joinable()) {
                entry.second.join();
            }
        }
    }

    void communicator(const std::shared_ptr<Client> &client_ptr, State *state) {
        // Create a message buffer for receiving the client's messages. The +1 is to simplify conversion into a valid,
        // null-terminated, C-string :)
        char buffer[config::MAX_MESSAGE_SIZE + 1];

        // Check for new messages from the client while the application and client are alive
        while (!state->kill && client_ptr->alive) {
            // Send pending messages on the queue
            if (communicator_outgoing(client_ptr, state)) {
                break;
            }

            // Read new command, if available
            if (communicator_incoming(client_ptr, state, buffer)) {
                break;
            }
        }

        // There is only two scenarios here: either our connection is dead or the application is being killed
        if (client_ptr->alive) {
            client_ptr->alive = false;
        }

        // Close connection socket, if not already closed
        if (client_ptr->connection.socket_fd >= 0) {
            close(client_ptr->connection.socket_fd);
            client_ptr->connection.socket_fd = -1;
        }

        // Exit :)
    }

    //////////////////////
    // Inbound Messages //
    //////////////////////

    bool communicator_incoming(const std::shared_ptr<Client> &client_ptr, State *state, char buffer[]) {
        // Try reading the next message from the client in a non-blocking way. If there is no message available, or
        // a timeout is triggered, a EAGAIN or EWOULDBLOCK error will happen, resulting in a -2 return.
        int result = network::read_message(client_ptr->connection.socket_fd, buffer);

        // If there is no available message or something wrong happened, try again after a small delay
        if (result == -2) {
            std::this_thread::sleep_for(config::POLLING_INTERVAL);
            return false;
        }

        // Check for a likely unrecoverable error, if present, end this connection
        if (result == -1) {
            client_ptr->alive = false;
            return true;
        }

        // Check for a closed connection from the client
        if (result == 0) {
            error::warning("The client with ip " + client_ptr->ip_str + " has ended its connection!");
            client_ptr->alive = false;
            return true;
        }

        // We have successfully received a message, result holds the actual message length, so we can build the
        // string by null-terminating it.
        buffer[result] = '\0';
        std::string message = buffer;

        // Do something with the message, not my problem...
        handle(message, client_ptr, state);
        return false;
    }

    ///////////////////////
    // Outbound Messages //
    ///////////////////////

    bool communicator_outgoing(const std::shared_ptr<Client> &client_ptr, State *state) {
        while (!state->kill && client_ptr->alive) {
            std::shared_ptr<std::string> message = client_ptr->pop_message();

            if (!message) {
                return false;
            }

            // Send direct response to client
            std::pair<const std::shared_ptr<Client>, int> client_info = std::make_pair(client_ptr, 0);

            // Try sending message until max tries reached
            while (try_send_message(*message, client_info)) {
                // Should be dead, skip
                if (state->kill || !client_ptr->alive) {
                    return true;
                }

                // Delay
                std::this_thread::sleep_for(config::POLLING_INTERVAL);
            }
        }
        return false;
    }

    void broadcast_message_channel(const std::string &message, const std::shared_ptr<Channel> &channel) {
        // Build a shared pointer to a copy of the message to avoid duplicating the message for each client which will
        // receive it.
        std::shared_ptr<std::string> message_ptr = std::make_shared<std::string>(message);

        for (const auto &entry: channel->members) {
            // Skip dead clients
            if (!entry->alive) {
                continue;
            }

            // Add the message to the queue of the current client
            entry->add_message(message_ptr);
        }
    }

    bool try_send_message(const std::string &message, std::pair<const std::shared_ptr<Client>, int> &client_info) {
        // If the connection has recently died, shouldn't be retried
        if (!client_info.first->alive) {
            return false;
        }

        // Try sending the message itself
        int result = network::send_message(client_info.first->connection.socket_fd,
                                           const_cast<char *>(message.c_str()),
                                           (int) std::min(message.size(), (size_t) config::MAX_MESSAGE_SIZE));

        // Failed to send message, likely due to timeout
        if (result == -2) {
            // Iterate tries counter
            client_info.second++;

            // If the maximum tries was reached, make the connection dead and don't retry it
            if (client_info.second >= config::MAX_SEND_TRIES) {
                client_info.first->alive = false;
                return false;
            }

            // Still should retry sending
            return true;
        }

        // Likely unrecoverable error, abort already
        if (result == -1) {
            error::error("Failed to send a message");
            client_info.first->alive = false;

            // No retry needed
            return false;
        }

        // No retry needed
        return false;
    }

    //////////////////////
    // Message Handling //
    //////////////////////

    // Validation //

    bool is_nickname_allowed(char c) {
        return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || ('0' <= c && c <= '9') || c == '-' || c == '_' ||
               c == '.';
    }

    // Parsing //

    void parse_msg_boundaries(const std::string &message, size_t &start, size_t &end) {
        start = 0;
        end = 0;

        for (size_t i = 1; i < message.size(); i++) {
            if (message[i] != ' ' && message[i - 1] == ' ') {
                if (start == 0) {
                    start = i;
                    continue;
                }
            }

            if (start != 0 && message[i] == ' ') {
                end = i;
            }
        }

        if (end == 0) {
            end = message.size();
        }
    }

    // Handlers //

    void handle_nick(const std::string &message, const std::shared_ptr<Client> &client_ptr, State *state) {
        size_t nick_st = 0;
        size_t nick_en = 0;

        parse_msg_boundaries(message, nick_st, nick_en);

        // Extract nickname
        std::shared_ptr<std::string> nick = std::make_shared<std::string>(message.substr(nick_st, nick_en));

        // Validate nickname size
        if (nick->empty() || nick->size() > 50) {
            client_ptr->add_message(std::make_shared<std::string>("Nickname size is invalid"));
            return;
        }

        // Validate nickname content
        for (const auto &c: *nick) {
            if (!is_nickname_allowed(c)) {
                client_ptr->add_message(std::make_shared<std::string>("Nickname not allowed"));
                return;
            }
        }

        bool failure = false;

        {
            auto guard = std::lock_guard<std::mutex>(state->registered_clients_mutex);

            // Removal failure
            if (state->registered_clients.find(*nick) != state->registered_clients.end()) {
                failure = true;
            } else {
                // Remove previous nick
                if (client_ptr->nickname) {
                    state->registered_clients.erase(*client_ptr->nickname);
                }

                // Register client
                state->registered_clients[*nick] = client_ptr;
                client_ptr->nickname = nick;
            }
        }

        if (failure) {
            client_ptr->add_message(std::make_shared<std::string>("Nickname not available"));
            return;
        }

        client_ptr->add_message(std::make_shared<std::string>("Nickname updated"));
    }

    void handle_kick(const std::string &message, const std::shared_ptr<Client> &client_ptr, State *state) {
        if (!client_ptr->channel) {
            client_ptr->add_message(std::make_shared<std::string>("You must be in a channel to kick someone"));
            return;
        }

        if (*client_ptr->channel->chop != *client_ptr->nickname) {
            client_ptr->add_message(std::make_shared<std::string>("You must be the channel operator to kick someone"));
            return;
        }

        size_t nick_st = 0;
        size_t nick_en = 0;

        parse_msg_boundaries(message, nick_st, nick_en);

        // Extract nickname
        std::string nick = message.substr(nick_st, nick_en);

        // Validate nickname size
        if (nick.empty() || nick.size() > 50) {
            client_ptr->add_message(std::make_shared<std::string>("Nickname size is invalid"));
            return;
        }

        if (nick == *client_ptr->nickname) {
            client_ptr->add_message(std::make_shared<std::string>("You cant kick yourself"));
            return;
        }

        std::shared_ptr<Client> target = nullptr;

        // Retrieve the target user pointer
        {
            auto guard = std::lock_guard<std::mutex>(state->registered_clients_mutex);

            auto it = state->registered_clients.find(nick);
            if (it != state->registered_clients.end()) {
                target = it->second;
            }
        }

        // Check if the user was found in the same channel
        if (!target || target->channel != client_ptr->channel) {
            client_ptr->add_message(std::make_shared<std::string>("The user is not present"));
            return;
        }

        // Remove the target from the members list
        {
            auto guard = std::lock_guard<std::mutex>(target->channel->mutex);

            // Remove target from members list
            target->channel->members.erase(target);
        }

        target->channel = nullptr;
        target->add_message(std::make_shared<std::string>("You were kicked from the channel"));
    }

    void handle_whois(const std::string &message, const std::shared_ptr<Client> &client_ptr, State *state) {
        if (!client_ptr->channel) {
            client_ptr->add_message(std::make_shared<std::string>("You must be in a channel to whois someone"));
            return;
        }

        if (*client_ptr->channel->chop != *client_ptr->nickname) {
            client_ptr->add_message(std::make_shared<std::string>("You must be the channel operator to whois someone"));
            return;
        }

        size_t nick_st = 0;
        size_t nick_en = 0;

        parse_msg_boundaries(message, nick_st, nick_en);

        // Extract nickname
        std::string nick = message.substr(nick_st, nick_en);

        // Validate nickname size
        if (nick.empty() || nick.size() > 50) {
            client_ptr->add_message(std::make_shared<std::string>("Nickname size is invalid"));
            return;
        }

        std::shared_ptr<Client> target = nullptr;

        // Retrieve the target user pointer
        {
            auto guard = std::lock_guard<std::mutex>(state->registered_clients_mutex);

            auto it = state->registered_clients.find(nick);
            if (it != state->registered_clients.end()) {
                target = it->second;
            }
        }

        // Check if the user was found in the same channel
        if (!target || target->channel != client_ptr->channel) {
            client_ptr->add_message(std::make_shared<std::string>("The user is not present"));
            return;
        }

        client_ptr->add_message(std::make_shared<std::string>(target->ip_str));
    }

    void handle_mute(const std::string &message, const std::shared_ptr<Client> &client_ptr, State *state) {
        if (!client_ptr->channel) {
            client_ptr->add_message(std::make_shared<std::string>("You must be in a channel to mute someone"));
            return;
        }

        if (*client_ptr->channel->chop != *client_ptr->nickname) {
            client_ptr->add_message(std::make_shared<std::string>("You must be the channel operator to mute someone"));
            return;
        }

        size_t nick_st = 0;
        size_t nick_en = 0;

        parse_msg_boundaries(message, nick_st, nick_en);

        // Extract nickname
        std::string nick = message.substr(nick_st, nick_en);

        // Validate nickname size
        if (nick.empty() || nick.size() > 50) {
            client_ptr->add_message(std::make_shared<std::string>("Nickname size is invalid"));
            return;
        }

        // Add the nick to the muted list
        {
            auto guard = std::lock_guard<std::mutex>(client_ptr->channel->mutex);
            client_ptr->channel->muted.insert(nick);
        }

        // Success message
        client_ptr->add_message(std::make_shared<std::string>("The nick '" + nick + "' is now muted in the channel!"));
    }

    void handle_unmute(const std::string &message, const std::shared_ptr<Client> &client_ptr, State *state) {
        if (!client_ptr->channel) {
            client_ptr->add_message(std::make_shared<std::string>("You must be in a channel to unmute someone"));
            return;
        }

        if (*client_ptr->channel->chop != *client_ptr->nickname) {
            client_ptr->add_message(std::make_shared<std::string>("You must bethe channel operator to unmute someone"));
            return;
        }

        size_t nick_st = 0;
        size_t nick_en = 0;

        parse_msg_boundaries(message, nick_st, nick_en);

        // Extract nickname
        std::string nick = message.substr(nick_st, nick_en);

        // Validate nickname size
        if (nick.empty() || nick.size() > 50) {
            client_ptr->add_message(std::make_shared<std::string>("Nickname size is invalid"));
            return;
        }

        // Remove the nick from the muted list
        {
            auto guard = std::lock_guard<std::mutex>(client_ptr->channel->mutex);
            client_ptr->channel->muted.erase(nick);
        }

        // Success message
        client_ptr->add_message(std::make_shared<std::string>("The nick '" + nick + "' is now unmuted in the channel!"));
    }

    void handle_join(const std::string &message, const std::shared_ptr<Client> &client_ptr, State *state) {
        if (!client_ptr->nickname) {
            client_ptr->add_message(
                    std::make_shared<std::string>("Identify yourself using /nick to be able to join a channel"));
            return;
        }

        size_t name_st = 0;
        size_t name_en = 0;

        parse_msg_boundaries(message, name_st, name_en);

        // Extract name
        std::string name = message.substr(name_st, name_en);

        // Check size constraint
        if (name.empty() || name.size() > 200) {
            client_ptr->add_message(std::make_shared<std::string>("Channel name size is invalid"));
            return;
        }

        // Check initial character constraint
        if (name[0] != '#' && name[0] != '&') {
            client_ptr->add_message(std::make_shared<std::string>("Channels must start with either # or &"));
            return;
        }

        // Validate that only allowed chars are present
        for (const auto &c: name) {
            if (c == ',' || c == 7 || c == ' ') {
                client_ptr->add_message(std::make_shared<std::string>("Channel name is not allowed"));
                return;
            }
        }

        std::shared_ptr<Channel> channel;

        // Acquire channels lock and retrieve/create the channel
        {
            auto guard = std::lock_guard<std::mutex>(state->channels_mutex);

            auto channel_it = state->channels.find(name);
            if (channel_it != state->channels.end()) {
                channel = channel_it->second;
            } else {
                channel = std::make_shared<Channel>();
                channel->name = name;
                channel->chop = client_ptr->nickname;
                channel->members.insert(client_ptr);
                state->channels[name] = channel;
            }
        }

        // Acquire the channel's mutex and join it
        {
            auto guard = std::lock_guard<std::mutex>(channel->mutex);

            // Check if the user is allowed to join the channel
            if (channel->banned.find(*client_ptr->nickname) != channel->banned.end()) {
                client_ptr->add_message(std::make_shared<std::string>("You are banned from this channel"));
                return;
            }

            // Add self to members list
            channel->members.insert(client_ptr);
        }

        // Leave any old channel
        if (client_ptr->channel) {
            auto guard = std::lock_guard<std::mutex>(client_ptr->channel->mutex);

            // Remove self from members list
            client_ptr->channel->members.erase(client_ptr);

            // If channel is empty, kill it
            if (client_ptr->channel->members.empty()) {
                auto guard2 = std::lock_guard<std::mutex>(state->channels_mutex);

                state->channels.erase(client_ptr->channel->name);
            }
        }

        // Update active channel
        client_ptr->channel = channel;
        client_ptr->add_message(std::make_shared<std::string>("Joined the channel!"));
    }

    void handle_text(const std::string &message, const std::shared_ptr<Client> &client_ptr) {
        if (!client_ptr->nickname) {
            client_ptr->add_message(
                    std::make_shared<std::string>("Identify yourself using /nick to be able to send a message"));
            return;
        }

        if (!client_ptr->channel) {
            client_ptr->add_message(
                    std::make_shared<std::string>("You must join a channel using /join to send a message"));
            return;
        }

        // Check if the client is muted in the channel
        if (client_ptr->channel->muted.find(*client_ptr->nickname) != client_ptr->channel->muted.end()) {
            client_ptr->add_message(
                    std::make_shared<std::string>("You are muted in this channel!"));
            return;
        }

        // Size of the nickname prefix
        size_t prefix_len = client_ptr->nickname->size() + 2;

        if (message.size() + prefix_len <= config::MAX_MESSAGE_SIZE) {
            // Broadcast message to every client
            broadcast_message_channel((*client_ptr->nickname) + ": " + message, client_ptr->channel);
        } else {
            // Split the message into two
            size_t cut_idx = config::MAX_MESSAGE_SIZE - prefix_len;
            std::string left_str = (*client_ptr->nickname) + ": " + message.substr(0, cut_idx);
            std::string right_str = (*client_ptr->nickname) + ": " + message.substr(cut_idx, message.size());

            // Send both messages
            broadcast_message_channel(left_str, client_ptr->channel);
            broadcast_message_channel(right_str, client_ptr->channel);
        }
    }

    // Message Entrypoint //

    void handle(const std::string &message, const std::shared_ptr<Client> &client_ptr, State *state) {
        // Check if it's a normal message, if it is, concatenate the client nickname and broadcast it
        if (message[0] != '/') {
            handle_text(message, client_ptr);
            return;
        }

        auto message_cstr = message.c_str();

        // Handle quit command
        if (strcasecmp("/quit", message_cstr) == 0) {
            client_ptr->alive = false;
            return;
        }

        // Handle connect command
        if (strcasecmp("/connect", message_cstr) == 0) {
            client_ptr->add_message(std::make_shared<std::string>("Already connected!"));
            return;
        }

        // Handle the ping command
        if (strcasecmp("/ping", message_cstr) == 0) {
            client_ptr->add_message(std::make_shared<std::string>("pong"));
            return;
        }

        if (strncasecmp("/nick", message_cstr, 5) == 0 && (message.size() <= 5 || message[5] == ' ')) {
            handle_nick(message, client_ptr, state);
            return;
        }

        if (strncasecmp("/join", message_cstr, 5) == 0 && (message.size() <= 5 || message[5] == ' ')) {
            handle_join(message, client_ptr, state);
            return;
        }

        if (strncasecmp("/kick", message_cstr, 5) == 0 && (message.size() <= 5 || message[5] == ' ')) {
            handle_kick(message, client_ptr, state);
            return;
        }

        if (strncasecmp("/mute", message_cstr, 5) == 0 && (message.size() <= 5 || message[5] == ' ')) {
            handle_mute(message, client_ptr, state);
            return;
        }

        if (strncasecmp("/unmute", message_cstr, 7) == 0 && (message.size() <= 7 || message[7] == ' ')) {
            handle_unmute(message, client_ptr, state);
            return;
        }

        if (strncasecmp("/whois", message_cstr, 6) == 0 && (message.size() <= 6 || message[6] == ' ')) {
            handle_whois(message, client_ptr, state);
            return;
        }

        // Unknown command
        client_ptr->add_message(std::make_shared<std::string>("Unknown command!"));
    }

    /////////////////////////////////////
    // Client message queue management //
    /////////////////////////////////////

    void Client::add_message(const std::shared_ptr<std::string> &message) {
        // Prevent concurrency while modifying the client's message queue
        auto guard = std::lock_guard<std::mutex>(this->message_queue_mutex);
        // Add the new message to the end of the message queue
        this->message_queue.push(message);
    }

    std::shared_ptr<std::string> Client::pop_message() {
        // Prevent concurrency while modifying the client's message queue
        auto guard = std::lock_guard<std::mutex>(this->message_queue_mutex);

        // If the queue is empty, return a null pointer
        if (this->message_queue.empty()) {
            return nullptr;
        }

        // Get the front element and remove it from the queue
        auto data = this->message_queue.front();
        this->message_queue.pop();
        return data;
    }
}
