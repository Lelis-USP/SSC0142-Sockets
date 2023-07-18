#pragma once

#include<arpa/inet.h>
#include<sys/socket.h>

#include "config.h"


namespace network {
    typedef struct Connection {
        struct sockaddr_in client_address;
        int socket_fd;
    } Connection;

    // Configure socket timeout
    int configure_socket(int socket_fd);

    int configure_blocking(int socket_fd, bool blocking);

    // Create, configure and bind a listening socket following the given network config
    int listen(config::ConnectionConfig config);

    // Connect to a server
    int connect(config::ConnectionConfig config);

    // Accept an incoming connection to the network
    Connection accept_conn(int listener_fd);

    // Read message from the target connection
    int read_message(int connection_fd, char *buffer);

    // Send message to the target connection
    int send_message(int connection_fd, char *buffer, int length);

    // Close connection
    int close(int connection_fd);

    std::string address_repr(const sockaddr_in &addr);
}