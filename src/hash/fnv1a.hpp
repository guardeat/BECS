#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>

namespace byte {

namespace detail {

template <typename Type_>
struct Fnv1aConstants;

template <>
struct Fnv1aConstants<std::uint64_t> {
  static constexpr std::uint64_t OFFSET = 0xcbf29ce484222325ULL;
  static constexpr std::uint64_t PRIME = 0x00000100000001B3ULL;
};

template <>
struct Fnv1aConstants<std::uint32_t> {
  static constexpr std::uint32_t OFFSET = 0x811c9dc5U;
  static constexpr std::uint32_t PRIME = 0x01000193U;
};

} // namespace detail

template <typename Type_>
concept Fnv1aHashable =
    std::same_as<Type_, std::uint64_t> || std::same_as<Type_, std::uint32_t>;

/// @brief Computes an FNV-1a hash of a string. Defaults to 64-bit (std::uint64_t).
template <Fnv1aHashable Type_ = std::uint64_t>
[[nodiscard]] constexpr Type_ fnv1a(std::string_view str) noexcept {
  using Constants = detail::Fnv1aConstants<Type_>;
  Type_ hash = Constants::OFFSET;
  for (const char c : str) {
    hash = (hash ^ static_cast<Type_>(static_cast<unsigned char>(c))) * Constants::PRIME;
  }
  return hash;
}

} // namespace byte
