#include<iostream>
#include<chrono>
#include<thread>

#include<unistd.h>

#include "../common/error.h"

#include "worker.h"

namespace worker::client {

    void handle_connect(State *state) {
        // Log connection start
        std::cout << "\rConnecting to " << state->config.host << ":" << state->config.port << std::endl;
        // Connect to the server with the given configuration
        int socket_fd = network::connect(state->config);
        state->socket_fd = socket_fd;
        std::cout << "\rConnected :)" << std::endl;
    }

    void handle_quit(State *state) {
        error::warning("Closing connection");
        state->kill = true;
    }

    void handle_message(std::string message, State *state) {
        // Handle connect command
        if (message == "/connect") {
            handle_connect(state);
            return;
        }

        // Handle quit command
        if (message == "/quit") {
            handle_quit(state);
            return;
        }

        // Handle other messages
        if (state->socket_fd == -1) {
            error::error("Not connected");
            return;
        }

        // Prevent concurrent access to message queue using mutex guard
        {
            auto guard = std::lock_guard<std::mutex>(state->message_queue_mutex);

            // Split the message into chunks
            for (size_t i = 0; i < message.size(); i += config::MAX_MESSAGE_SIZE) {
                std::string sub_msg = message.substr(i, std::min(message.size(), i + config::MAX_MESSAGE_SIZE));

                // Handle unintended command abnormality
                if (i != 0 && sub_msg[0] == '/') {
                    sub_msg[0] = '\\';
                }

                // Add message chunk to the queue
                state->pending_messages.push(sub_msg);
            }
        }
    }

    void manager(State *state) {
        // Initiate input manager in a separate, detached, thread
        std::thread input_thread(input, state);
        input_thread.detach();

        // Run communicator inside this thread
        communicator(state);
    }

    void communicator(State *state) {
        // Wait for a connection to be initiated
        while (!state->kill && state->socket_fd == -1) {
            std::this_thread::sleep_for(config::POLLING_INTERVAL);
        }

        const int local_fd = state->socket_fd;

        // Message buffer
        char buffer[config::MAX_MESSAGE_SIZE + 1];

        // Read/send messages from/to the connection
        while (!state->kill) {
            // Accept next incoming message, if present
            if (communicator_incoming(state, buffer)) {
                break;
            }

            // Send next pending message, if present
            if (communicator_outgoing(state)) {
                break;
            }
        }

        // Something happened that the connection was ended, let's kill the application then
        if (!state->kill) {
            state->kill = true;
        }

        // Close connection
        if (state->socket_fd != -1) {
            close(state->socket_fd);
            state->socket_fd = -1;
        }
    }

    void input(State *state) {
        // Input reader
        while (!std::cin.bad() && !std::cin.eof() && !state->kill) {
            // Read message
            std::cout << "> ";
            std::string message;
            std::getline(std::cin, message);

            // Remove trailing whitespace
            message.erase(message.find_last_not_of(" \n\r\t") + 1);

            // Handle message
            handle_message(message, state);
        }

        // Something happened, likely a kill or EOF, in either case the whole application should come down
        if (!state->kill) {
            state->kill = true;
        }
    }

    bool communicator_incoming(State *state, char buffer[]) {
        // Try getting the next pending message from the server
        int result = network::read_message(state->socket_fd, buffer);

        // If there is no message available for now, sleep for a bit and try again
        if (result == -2) {
            std::this_thread::sleep_for(config::POLLING_INTERVAL);
            return false;
        }

        // If there was an unrecoverable error from the communication, abort the listener
        if (result == -1) {
            return true;
        }

        // Check for a closed connection from the server
        if (result == 0) {
            std::cout << "\rConnection closed from the server!" << std::endl;
            return true;
        }

        // A new message from the server is available, print it :)
        buffer[result] = '\0';
        std::cout << "\r" << buffer << std::endl;

        // Print input caret back
        std::cout << "> ";
        std::flush(std::cout);

        return false;
    }

    bool communicator_outgoing(State *state) {
        if (state->pending_messages.empty()) {
            return false;
        }

        // Get front message from queue
        std::string message = state->pending_messages.front();

        // Acquire lock and remove front message from queue
        {
            auto guard = std::lock_guard<std::mutex>(state->message_queue_mutex);
            state->pending_messages.pop();
        }

        // Try sending
        int tries = 0;

        while (!state->kill) {
            // Try sending the message
            int result = network::send_message(state->socket_fd, const_cast<char *>(message.c_str()),
                                               (int) std::min(message.size(), (size_t) config::MAX_MESSAGE_SIZE));

            // Failed to send the message (likely a timeout)
            if (result == -2) {
                tries++;

                // Exceeded maximum tries, connection must be dead
                if (tries >= config::MAX_SEND_TRIES) {
                    error::error("Failed to send message (maximum tries reached)!");
                    return true;
                }

                // Cool down for a little bit
                std::this_thread::sleep_for(config::POLLING_INTERVAL);
                continue;
            }

            // An unrecoverable error has happened, abort
            if (result == -1) {
                return true;
            }

            // Message successfully sent, just break :)
            break;
        }

        return false;
    }
}