#pragma once

#include <stdexcept>
#include <iostream>

#define ASSERT(cond, msg)\
do{\
    if (!(cond)){\
        throw std::runtime_error(msg);\
    }\
}while(false)

#define LOG(msg) std::cout << (msg) << std::endl
