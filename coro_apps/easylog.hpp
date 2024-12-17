#pragma once
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

#if __has_include(<format>)
#include <format>
#endif

#if __has_include(<fmt/format.h>)
#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY
#endif
#endif

// #include 