#include<iostream>
#include<thread>
#include<mutex>
#include<memory>
#include<iomanip>
#include<set>

#include "../common/error.h"
#include "../common/worker.h"

#include "worker.h"

namespace worker::server {

    /**
     * Accept a new client collection using a non-blocking polling strategy
     *
     * @param state the application shared state pointer (holds data shared across threads)
     * @param client_number the next client number (used for nickname generator)
     * @return the new client's shared_ptr, can be a nullptr if failed to accept a new connection for any reason
     */
    std::shared_ptr<Client> accept_client(State *state, int client_number) {

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

        // We have successfully connected to a new client, let's compose it's data

        // Build the client's nickname
        std::stringstream nick_ss;
        nick_ss << "client-" << client_number;

        // Build new client using a shared pointer (easier to manage memory)
        std::shared_ptr<Client> client_ptr = std::make_shared<Client>();
        client_ptr->connection = conn; // Client's connection data, include the socket fd
        client_ptr->ip_str = network::address_repr(conn.client_address); // Parse the client IP into a string
        client_ptr->nickname = nick_ss.str(); // The client nickname
        client_ptr->alive = true; // If the client is alive and happy :)

        // Return the shared pointer
        return client_ptr;
    }

    /**
     * Client manager thread runner. Checks for incoming connections and spin-up new listener for them.
     * @param state the application shared state.
     */
    void manager(State *state) {
        // If no state, exit
        if (state == nullptr) {
            return;
        }

        // Client number for nickname generation
        int client_number = 0;

        // While the application should be alive, accept new clients
        while (!state->kill) {
            // Try accepting a new client
            std::shared_ptr<Client> client_ptr = accept_client(state, ++client_number);

            // Check if a new client was successfully accepted
            if (!client_ptr) {
                // Most errors of this kind aren't recoverable, so we'll trigger a kill here
                state->kill = true;
                break;
            }

            // We have a new connection, register it to the clients map. Here we do guarded access to the map to avoid
            // any concurrency issues. The guard is destroyed after leaving the context, releasing the lock.
            {
                auto guard = std::lock_guard<std::mutex>(state->clients_mutex);
                state->clients[client_ptr->nickname] = client_ptr;
            }

            // Here we can do one of two strategies: create a new thread for each client or have a separate thread
            // handling the connection to every client. The first option, although heavier, should be easier to
            // implement, so we'll follow with it!
            // Update: a new strategy which could also be good is an alternating strategy (try receiving, then send) on
            // each connections' thread alongside a message queue with the pending messages for that client. That way
            // there is no chance of sync issues for the client.
            // Maybe event keep a simple message index alongside a global message history?
            std::thread listener_thread(communicator, client_ptr, state);
            listener_thread.detach();
        }

        // The only we should reach here is if a kill signal was received, but let's ensure it anyway to prevent future
        // bugs.
        if (!state->kill) {
            state->kill = false;
        }
    }

    void communicator(std::shared_ptr<Client> client_ptr, State *state) {

        // Create a message buffer for receiving the client's messages. The +1 is to simplify conversion into a valid,
        // null-terminated, C-string :)
        char buffer[config::MAX_MESSAGE_SIZE + 1];

        // Check for new messages from the client while the application and client are alive
        while (!state->kill && client_ptr->alive) {
            // Try reading the next message from the client in a non-blocking way. If there is no message available, or
            // a timeout is triggered, a EAGAIN or EWOULDBLOCK error will happen, resulting in a -2 return.
            int result = network::read_message(client_ptr->connection.socket_fd, buffer);

            // If there is no available message or something wrong happened, try again after a small delay
            if (result == -2) {
                std::this_thread::sleep_for(config::POLLING_INTERVAL);
                continue;
            }

            // Check for a likely unrecoverable error, if present, end this connection
            if (result == -1) {
                client_ptr->alive = false;
                break;
            }

            // Check for a closed connection
            if (result == 0) {
                client_ptr->alive = false;
                break;
            }

            // We have successfully received a message, result holds the actual message length, so we can build the
            // string by null-terminating it.
            buffer[result] = '\0';
            std::string message = buffer;

            // Do something with the message, not my problem...
            handle_message(message, client_ptr, state);
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


    std::unordered_map<std::shared_ptr<Client>, int> get_pending_clients(State *state) {
        // Create a map of pointers, holding the number of failures whilst trying to send the message to each client
        std::unordered_map<std::shared_ptr<Client>, int> pending_clients;

        // Iterate over each connection and add it to our pending clients set. Keep track of the concurrency mutex to
        // avoid any modification to our map while building the set.
        auto guard = std::lock_guard<std::mutex>(state->clients_mutex);
        for (const auto &entry: state->clients) {
            if (!entry.second->alive) {
                continue;
            }
            pending_clients[entry.second] = 0;
        }

        return pending_clients;
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

        if (result == -1) {
            error::error("Failed to send a message");
            // No retry needed
            return false;
        }

        // No retry needed
        return false;
    }

    void handle_message(const std::string &message, std::shared_ptr<Client> client_ptr, State* state) {
        // Check if it's a normal message, if it is, concatenate the client nickname and broadcast it
        if (message[0] != '/') {
            // TODO: fix truncation of message

            // Add author's nickname to message
            std::stringstream new_message;
            new_message << client_ptr->nickname << ": " << message;

            // Broadcast message to every client
            broadcast_message(new_message.str(), state);
            return;
        }

        // Handle each command
        if (message == "/quit") {
            client_ptr->alive = false;
            return;
        }

        // Response stream
        std::stringstream response_ss;

        // Connect command
        if (message == "/connect") {
            response_ss << "already connected";
        }

        // Ping command
        if (message == "/ping") {
            response_ss << "pong";
        }

        // Send direct response to client
        std::pair<const std::shared_ptr<Client>, int> client_info = std::make_pair(client_ptr, 0);
        std::string response = response_ss.str();

        // Try sending message until max tries reached
        while (try_send_message(response, client_info)) {
            std::this_thread::sleep_for(config::POLLING_INTERVAL);
        }
    }


    void broadcast_message(const std::string &message, State *state) {
        auto pending_clients = get_pending_clients(state);

        // Try sending the messages to each of the alive clients, unless a kill is requested
        while (!state->kill && !pending_clients.empty()) {
            // Iterate over each client
            auto it = pending_clients.begin();
            while (it != pending_clients.end()) {
                // Kill signal, just stop all the chaos
                if (state->kill) {
                    break;
                }

                if (try_send_message(message, *it)) {
                    // The message delivery timed out and should still be retried
                    it++;
                } else {
                    // The message shouldn't be retried anymore, it either failed or was successfully sent
                    it = pending_clients.erase(it);
                }
            }

            std::this_thread::sleep_for(config::POLLING_INTERVAL);
        }
    }


}