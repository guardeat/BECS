#pragma once

#include "fnv1a.hpp"
#include "type_name.hpp"

#include <cstdint>

namespace byte {

using TypeID = std::uint64_t;

/// @brief Returns a compile-time deterministic TypeID based on the FNV-1a hash of the type's name.
template <typename Type_>
[[nodiscard]] consteval TypeID type_id() noexcept {
  return fnv1a<TypeID>(type_name<Type_>());
}

/// @brief Variable template helper for compile-time TypeID: byte::type_id_v<Position>
template <typename Type_>
inline constexpr TypeID type_id_v = type_id<Type_>();

} // namespace byte