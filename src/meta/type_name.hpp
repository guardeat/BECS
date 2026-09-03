#pragma once

#include <string_view>

namespace byte {

namespace detail {

template <typename Type_>
constexpr std::string_view raw_type_name() noexcept {
#if defined(__clang__) || defined(__GNUC__)
  return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
  return __FUNCSIG__;
#else
  return "";
#endif
}

struct TypeNameProbeTarget {};

consteval std::size_t probe_prefix_length() noexcept {
  const std::string_view sample = raw_type_name<TypeNameProbeTarget>();
  constexpr std::string_view target = "byte::detail::TypeNameProbeTarget";
  constexpr std::string_view short_target = "TypeNameProbeTarget";

  std::size_t pos = sample.find(target);
  if (pos != std::string_view::npos) {
    return pos;
  }
  return sample.find(short_target);
}

consteval std::size_t probe_suffix_length() noexcept {
  const std::string_view sample = raw_type_name<TypeNameProbeTarget>();
  constexpr std::string_view target = "byte::detail::TypeNameProbeTarget";
  constexpr std::string_view short_target = "TypeNameProbeTarget";

  std::size_t pos = sample.find(target);
  if (pos != std::string_view::npos) {
    return sample.size() - pos - target.size();
  }
  pos = sample.find(short_target);
  return sample.size() - pos - short_target.size();
}

} // namespace detail

/// @brief Returns the clean name of a type as a compile-time std::string_view.
template <typename Type_>
[[nodiscard]] consteval std::string_view type_name() noexcept {
  constexpr std::string_view raw = detail::raw_type_name<Type_>();
  if constexpr (raw.empty()) {
    return "UnknownType";
  } else {
    constexpr std::size_t prefix_len = detail::probe_prefix_length();
    constexpr std::size_t suffix_len = detail::probe_suffix_length();

    std::string_view name = raw.substr(prefix_len, raw.size() - prefix_len - suffix_len);

#if defined(_MSC_VER)
    // MSVC prepends 'struct ', 'class ', or 'enum '
    if (name.starts_with("struct ")) {
      name.remove_prefix(7);
    } else if (name.starts_with("class ")) {
      name.remove_prefix(6);
    } else if (name.starts_with("enum ")) {
      name.remove_prefix(5);
    }
#endif

    return name;
  }
}

} // namespace byte