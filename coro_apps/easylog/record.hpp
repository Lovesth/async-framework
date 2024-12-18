#pragma once

#include <charconv>
#include <chrono>
#include <cstring>

#include "../util/time_util.h"
#if __has_include(<memory_resource>)
#include <memory_resource>
#endif

#if defined(__linux__) || defined(__FreeBSD__)
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__rtems__)
#include <rtems.h>
#endif

#include <sstream>
#include <string>
#include <string_view>
#include <utility>

