#include<iostream>
#include<chrono>
#include<thread>

#include "../common/error.h"
#include "../common/worker.h"

#include "worker.h"

namespace worker::server {
    void* manager(void* params) {
        if (params == nullptr) {
            return nullptr;
        }

        auto state = (State*) params;
        int socket_fd = state->socket_fd;

        // Accept the next incoming connection
        network::Connection conn = network::accept_conn(socket_fd);
        if (conn.socket_fd == -1) {
            error::error("Connection failed!");
            return nullptr;
        }
        state->connection = conn;

        // Log incoming connection
        std::cout << "New incoming connection from " <<  network::address_repr(conn.client_address) << std::endl;

        // Issue new connection listener
        if (dispatch_thread(state->listener_tid, listener, state) != 0) {
            error::error("Failed to spin-up listener thread!");
            return nullptr;
        }

        while (!std::cin.bad() && !std::cin.eof()) {
            // Read message
            std::cout << "> ";
            std::string message;
            std::getline(std::cin, message);

            // Remove trailing whitespace
            message.erase(message.find_last_not_of(" \n\r\t") + 1);

            if (message == "/quit") {
                break;
            }

            network::send_message(state->connection.socket_fd, message);
        }

        // Kill listener
        if (state->socket_fd != -1) {
            close(state->socket_fd);
            state->socket_fd = -1;
        }
        if (state->connection.socket_fd != -1) {
            close(state->connection.socket_fd);
            state->connection.socket_fd = -1;
        }
        if (state->listener_tid > 0) {
            pthread_cancel(state->listener_tid);
        }
        return nullptr;
    }

    void* listener(void *params) {
        auto state = (State*) params;

        network::Connection conn = state->connection;

        // Message buffer
        char buffer[config::MAX_MESSAGE_SIZE + 1];
        // Receive messages in a loop
        while (true) {
            // Read the next message from the connection
            int result = network::read_message(conn.socket_fd, buffer);

            // If result length is 0 or less, there was either a termination, or an error
            if (result <= 0) {
                error::warning("Connection ended!");
                break;
            }

            buffer[result] = '\0';
            std::cout << "\rclient: " << buffer << std::endl;
            std::cout << "> ";
            std::flush(std::cout);

            // Sleep to simulate load
            std::this_thread::sleep_for(std::chrono::nanoseconds(100));
        }

        // Kill manager
        if (state->socket_fd != -1) {
            close(state->socket_fd);
            state->socket_fd = -1;
        }
        if (state->connection.socket_fd != -1) {
            close(state->connection.socket_fd);
            state->connection.socket_fd = -1;
        }
        if (state->manager_tid > 0) {
            pthread_cancel(state->manager_tid);
        }
        return nullptr;
    }
}