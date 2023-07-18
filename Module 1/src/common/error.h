#pragma once

#include<string>

namespace error {
    void critical_error(std::string msg);

    void error(std::string msg);

    void warning(std::string msg);
}