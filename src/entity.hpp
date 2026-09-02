#pragma once

#include <cstdint>

namespace byte::ecs {

struct EntityID {
  std::uint64_t value{};

  constexpr EntityID() = default;
  constexpr explicit EntityID(std::uint64_t raw) noexcept : value{raw} {}

  [[nodiscard]] constexpr explicit operator std::uint64_t() const noexcept {
    return value;
  }

  constexpr bool operator==(const EntityID&) const = default;
};

} // namespace byte::ecs
