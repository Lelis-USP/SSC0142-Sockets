#include<iostream>
#include<cerrno>

#include "error.h"

namespace error {
    void critical_error(std::string msg) {
        std::cerr << "\r[ERROR] " << msg;
        if (errno != 0) {
            std::cerr << "; ERRNO: " << errno;
        }
        std::cerr << std::endl;
        exit(1);
    }

    void error(std::string msg) {
        std::cerr << "\r[ERROR] " << msg;
        if (errno != 0) {
            std::cerr << "; ERRNO: " << errno;
        }
        std::cerr << std::endl;
    }

    void warning(std::string msg) {
        std::cerr << "\r[WARN] " << msg << std::endl;
    }
}