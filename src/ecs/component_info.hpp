#pragma once

#include "type_id.hpp"

#include <concepts>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace byte::ecs {

/// @brief Per-type metadata and optional thunks. Null pointer = memcpy or skip.
struct ComponentInfo {
  TypeID id;
  std::size_t size;
  std::size_t alignment;
  bool trivially_copyable;
  bool trivially_destructible;
  bool trivially_default_constructible;

  void (*copy_construct)(std::byte* dst, const std::byte* src);  // null: memcpy or not copyable
  void (*move_construct)(std::byte* dst, std::byte* src);        // null: memcpy relocate
  void (*destroy)(std::byte* p) noexcept;                        // null: skip
  void (*default_construct)(std::byte* p);                       // null: skip or not default constructible

  [[nodiscard]] bool copyable() const noexcept {
    return trivially_copyable || copy_construct != nullptr;
  }

  [[nodiscard]] bool default_constructible() const noexcept {
    return trivially_default_constructible || default_construct != nullptr;
  }
};

namespace detail {

template <typename Type_>
void copy_construct(std::byte* dst, const std::byte* src) {
  std::construct_at(reinterpret_cast<Type_*>(dst), *reinterpret_cast<const Type_*>(src));
}

template <typename Type_>
void move_construct(std::byte* dst, std::byte* src) {
  std::construct_at(reinterpret_cast<Type_*>(dst), std::move(*reinterpret_cast<Type_*>(src)));
}

template <typename Type_>
void destroy(std::byte* p) noexcept {
  std::destroy_at(reinterpret_cast<Type_*>(p));
}

template <typename Type_>
void default_construct(std::byte* p) {
  std::construct_at(reinterpret_cast<Type_*>(p));
}

template <typename Type_>
[[nodiscard]] constexpr ComponentInfo make_component_info() noexcept {
  using CopyFn = void (*)(std::byte*, const std::byte*);
  using MoveFn = void (*)(std::byte*, std::byte*);
  using DestroyFn = void (*)(std::byte*) noexcept;
  using DefaultFn = void (*)(std::byte*);

  CopyFn copy = nullptr;
  if constexpr (!std::is_trivially_copyable_v<Type_> && std::copy_constructible<Type_>) {
    copy = &copy_construct<Type_>;
  }

  MoveFn move = nullptr;
  if constexpr (!(std::is_trivially_copyable_v<Type_> ||
                  (std::is_trivially_move_constructible_v<Type_> &&
                   std::is_trivially_destructible_v<Type_>))) {
    move = &move_construct<Type_>;
  }

  DestroyFn dtor = nullptr;
  if constexpr (!std::is_trivially_destructible_v<Type_>) {
    dtor = &destroy<Type_>;
  }

  DefaultFn def = nullptr;
  if constexpr (!std::is_trivially_default_constructible_v<Type_> &&
                std::default_initializable<Type_>) {
    def = &default_construct<Type_>;
  }

  return ComponentInfo{
      .id = byte::type_id_v<Type_>,
      .size = sizeof(Type_),
      .alignment = alignof(Type_),
      .trivially_copyable = std::is_trivially_copyable_v<Type_>,
      .trivially_destructible = std::is_trivially_destructible_v<Type_>,
      .trivially_default_constructible = std::is_trivially_default_constructible_v<Type_>,
      .copy_construct = copy,
      .move_construct = move,
      .destroy = dtor,
      .default_construct = def,
  };
}

} // namespace detail

template <typename Type_>
inline constexpr ComponentInfo component_info_v = detail::make_component_info<Type_>();

template <typename Type_>
[[nodiscard]] const ComponentInfo& component_info() noexcept {
  return component_info_v<Type_>;
}

} // namespace byte::ecs
