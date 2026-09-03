#pragma once

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <string_view>

/// @brief Compile-time switch for byte::ecs precondition checks.
/// Defaults to 1 in debug builds (NDEBUG not defined), 0 in release (NDEBUG).
/// Override by defining BYTE_ECS_CHECKS before including this header.
#ifndef BYTE_ECS_CHECKS
  #ifdef NDEBUG
    #define BYTE_ECS_CHECKS 0
  #else
    #define BYTE_ECS_CHECKS 1
  #endif
#endif

namespace byte::ecs {

inline constexpr bool checks_enabled = static_cast<bool>(BYTE_ECS_CHECKS);

/// @brief Reports a failed precondition and aborts. Cold path only.
[[noreturn]] inline void fail(
    std::string_view message,
    std::source_location location = std::source_location::current()) {
  std::cerr << location.file_name() << ':' << location.line() << ':' << location.column()
            << ": byte::ecs assertion failed: " << message << '\n';
  std::abort();
}

/// @brief Checks a precondition. No-op when BYTE_ECS_CHECKS is 0.
inline void check(
    bool condition,
    std::string_view message,
    std::source_location location = std::source_location::current()) {
  if constexpr (checks_enabled) {
    if (!condition) [[unlikely]] {
      fail(message, location);
    }
  }
}

} // namespace byte::ecs
