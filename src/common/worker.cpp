#include<thread>

#include "worker.h"

namespace worker {
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
}