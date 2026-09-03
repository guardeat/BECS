#pragma once

#include "component_info.hpp"
#include "ecs_assert.hpp"

#include <cstddef>
#include <cstring>
#include <memory>

namespace byte::ecs {

/// @brief Type-erased component buffer. Size and capacity live on Archetype.
struct Column {
public:
  const ComponentInfo* info = nullptr;
  std::byte* data = nullptr;
  std::byte* raw = nullptr;
  std::size_t alloc_bytes = 0;

public:
  [[nodiscard]] std::byte* slot(std::size_t index) noexcept {
    return data + index * info->size;
  }

  [[nodiscard]] const std::byte* slot(std::size_t index) const noexcept {
    return data + index * info->size;
  }

  void destroy_slot(std::size_t index) {
    if (info->destroy != nullptr) {
      info->destroy(slot(index));
    }
  }

  void destroy_range(std::size_t n) {
    if (info->destroy == nullptr || data == nullptr) {
      return;
    }
    for (std::size_t i = 0; i < n; ++i) {
      info->destroy(slot(i));
    }
  }

  void default_construct_slot(std::size_t index) {
    if (info->default_construct != nullptr) {
      info->default_construct(slot(index));
    } else if (info->destroy != nullptr) {
      fail("column: type is not default constructible");
    }
  }

  void move_construct_slot(std::size_t dst_index, Column& src, std::size_t src_index) {
    std::byte* d = slot(dst_index);
    std::byte* s = src.slot(src_index);
    if (src.info->move_construct != nullptr) {
      src.info->move_construct(d, s);
    } else {
      std::memcpy(d, s, src.info->size);
    }
  }

  void relocate_range(Column& src, std::size_t n) {
    if (n == 0) {
      return;
    }
    if (src.info->move_construct == nullptr) {
      std::memcpy(data, src.data, n * src.info->size);
      return;
    }
    for (std::size_t i = 0; i < n; ++i) {
      src.info->move_construct(slot(i), src.slot(i));
    }
    src.destroy_range(n);
  }

  void swap_remove(std::size_t index, std::size_t n) {
    check(index < n, "swap_remove: index out of range");
    if (index + 1 < n) {
      destroy_slot(index);
      move_construct_slot(index, *this, n - 1);
      destroy_slot(n - 1);
    } else {
      destroy_slot(index);
    }
  }

  template <typename ByteAlloc_>
  void allocate(ByteAlloc_& alloc, std::size_t capacity) {
    if (capacity == 0) {
      return;
    }
    const std::size_t nbytes = capacity * info->size;
    const std::size_t alignment = info->alignment;
    const std::size_t total = nbytes + alignment;
    std::byte* allocated = std::allocator_traits<ByteAlloc_>::allocate(alloc, total);
    void* p = allocated;
    std::size_t space = total;
    void* aligned = std::align(alignment, nbytes, p, space);
    if (aligned == nullptr) {
      std::allocator_traits<ByteAlloc_>::deallocate(alloc, allocated, total);
      fail("column: aligned allocate failed");
    }
    raw = allocated;
    data = static_cast<std::byte*>(aligned);
    alloc_bytes = total;
  }

  template <typename ByteAlloc_>
  void deallocate(ByteAlloc_& alloc) {
    if (raw == nullptr) {
      return;
    }
    std::allocator_traits<ByteAlloc_>::deallocate(alloc, raw, alloc_bytes);
    raw = nullptr;
    data = nullptr;
    alloc_bytes = 0;
  }
};

} // namespace byte::ecs
