#pragma once

#include<unistd.h>

#include "network.h"

namespace worker {
    typedef struct ClientData {
        int socket_fd;
        pthread_t manager_tid;
        pthread_t listener_tid;
    } ClientData;

    int dispatch_thread(pthread_t &tid, void *(*runner)(void *), void *data);
}