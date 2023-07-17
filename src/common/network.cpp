#include<iostream>
#include<cstring>

#include<unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>

#include "config.h"
#include "error.h"

#include "network.h"

namespace network {
    // Create, configure and bind a listening socket following the given network config
    int listen(config::ConnectionConfig config) {
        // Create an IPv4 TCP socket
        int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0) { // Check if a file descriptor was allocated
            error::critical_error("Failed to create socket!");
            return -1;
        }

        // Build socket address for binding
        struct sockaddr_in listen_address{};
        std::memset(&listen_address, 0, sizeof(listen_address)); // Ensure no garbage is present
        listen_address.sin_family = AF_INET; // Use IPv4 addresses
        listen_address.sin_addr.s_addr = inet_addr(config.host.c_str()); // Listen to given address
        listen_address.sin_port = htons(config.port); // Listen on the configured network port

        // Bind socket to the configured address
        if (bind(socket_fd, reinterpret_cast<const sockaddr *>(&listen_address), sizeof(listen_address)) < 0) {
            error::critical_error("Failed to bind socket!");
            return -1;
        }

        // Listen on the created socket, with a limit of 63 pending connections
        if (::listen(socket_fd, 63) < 0) {
            error::critical_error("Failed to listen on socket!");
            return -1;
        }

        std::cout << "Server listening at " << config.host << ":" << config.port << std::endl;
        return socket_fd;
    }

    // Connect to a server
    int connect(config::ConnectionConfig config) {
        // Create an IPv4 TCP socket
        int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0) { // Check if a file descriptor was allocated
            error::critical_error("Failed to create socket!");
            return -1;
        }

        // Build socket address for connection
        struct sockaddr_in server_address{};
        std::memset(&server_address, 0, sizeof(server_address)); // Ensure no garbage is present
        server_address.sin_family = AF_INET; // Use IPv4 addresses
        server_address.sin_addr.s_addr = inet_addr(config.host.c_str()); // Connect to given address
        server_address.sin_port = htons(config.port); // Connect to given port

        // Connect to the given client
        if (::connect(socket_fd, reinterpret_cast<const sockaddr *>(&server_address), sizeof(server_address)) < 0) {
            error::critical_error("Failed to connect socket!");
            return -1;
        }

        std::cout << "Connected to " << config.host << ":" << config.port << std::endl;
        return socket_fd;
    }


    // Accept an incoming connection to the network
    Connection accept_conn(int listener_fd) {

        // Build connection data structure
        Connection new_conn{};
        std::memset(&new_conn, 0, sizeof(struct Connection));
        unsigned int addr_len = sizeof(Connection::client_address);

        // Accept next available connection (blocking)
        int conn_fd = ::accept(listener_fd, reinterpret_cast<sockaddr *>(&new_conn.client_address), &addr_len);
        if (conn_fd < 0) {
            error::critical_error("Failed to accept connection!");
            new_conn.socket_fd = -1;
            return new_conn;
        }

        // Update client connection file descriptor
        new_conn.socket_fd = conn_fd;
        return new_conn;
    }

    // Read message from the target connection
    int read_message(int connection_fd, char *buffer) {
        ssize_t received = recv(connection_fd, buffer, config::MAX_MESSAGE_SIZE, 0);

        if (received < 0) {
            error::critical_error("Receive error!");
            return -1;
        }

        if (received == 0) {
            error::warning("Received zero bytes, connection probably closed!");
            return 0;
        }

        return (int) received;
    }

    // Send message to the target connection
    int send_message(int connection_fd, char *buffer, int length) {
        ssize_t sent = send(connection_fd, buffer, length, 0);

        if (sent < 0) {
            error::critical_error("Send error!");
            return -1;
        }

        return (int) sent;
    }

    // Send message to the target connection
    int send_message(int connection_fd, const std::string &message) {
        const char *msg_cstr = message.c_str();
        for (size_t idx = 0; idx < message.size(); idx += config::MAX_MESSAGE_SIZE) {
            size_t data_length = std::min((size_t) message.size() - idx, (size_t) config::MAX_MESSAGE_SIZE);

            ssize_t sent = send(connection_fd, msg_cstr + idx, data_length, 0);

            if (sent < 0) {
                error::critical_error("Send error!");
                return -1;
            }

        }

        return (int) message.size();
    }

    // Close connection
    int close(int connection_fd) {
        int status = ::close(connection_fd);
        if (status == -1) {
            error::critical_error("Failed to close socket!");
            return -1;
        }

        return 0;
    }

    std::string address_repr(const sockaddr_in &addr) {
        char buffer[32];
        if (inet_ntop(AF_INET, &addr.sin_addr, buffer, 32) == nullptr) {
            return "?";
        }

        return {buffer};
    }
}
