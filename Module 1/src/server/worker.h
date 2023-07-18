#pragma once

#include<vector>

#include<unistd.h>

#include "../common/network.h"

namespace worker::server {
        struct State {
            network::Connection connection;
            int socket_fd;
            pthread_t manager_tid;
            pthread_t listener_tid;
        };

        void* manager(void* params);
        void* listener(void *params);
    }