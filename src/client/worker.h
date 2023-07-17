#pragma once

#include<vector>

#include<unistd.h>

#include "../common/network.h"

namespace worker::client {
    struct State {
        int socket_fd;
        pthread_t manager_tid;
        pthread_t listener_tid;
    };

    void* speaker(void* params);
    void* listener(void *params);
}