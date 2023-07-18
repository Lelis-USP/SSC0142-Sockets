#include<iostream>
#include<chrono>
#include<thread>

#include<unistd.h>

#include "../common/error.h"
#include "../common/worker.h"

#include "worker.h"

namespace worker::client {
    int dispatch_thread(pthread_t &tid, void *(*runner)(void *), void *data) {
        pthread_attr_t attr{};
        if (pthread_attr_init(&attr) != 0) {
            return -1;
        }

        if (pthread_create(&tid, &attr, runner, data) != 0) {
            return -1;
        }

        return 0;
    }

    void *speaker(void *params) {
        if (params == nullptr) {
            return nullptr;
        }

        auto client_data = (State *) params;

        // Issue new client listener
        if (dispatch_thread(client_data->listener_tid, listener, client_data) != 0) {
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

            network::send_message(client_data->socket_fd, message);
        }

        // Kill listener
        if (client_data->socket_fd != -1) {
            close(client_data->socket_fd);
            client_data->socket_fd = -1;
        }
        if (client_data->listener_tid > 0) {
            pthread_cancel(client_data->listener_tid);
        }
        return nullptr;
    }

    void *listener(void *params) {
        auto client_data = (State *) params;

        // Message buffer
        char buffer[config::MAX_MESSAGE_SIZE + 1];
        // Receive messages in a loop
        while (true) {
            // Read the next message from the connection
            int result = network::read_message(client_data->socket_fd, buffer);

            // If result length is 0 or less, there was either a termination, or an error
            if (result <= 0) {
                error::warning("Connection ended!");
                break;
            }

            buffer[result] = '\0';
            std::cout << "\rserver: " << buffer << std::endl;
            std::cout << "> ";
            std::flush(std::cout);

            // Sleep to simulate load
            std::this_thread::sleep_for(std::chrono::nanoseconds(100));
        }

        // Kill manager
        if (client_data->socket_fd != -1) {
            close(client_data->socket_fd);
            client_data->socket_fd = -1;
        }
        if (client_data->manager_tid > 0) {
            pthread_cancel(client_data->manager_tid);
        }
        return nullptr;
    }
}