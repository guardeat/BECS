#pragma once

#include "archeview.hpp"
#include "column.hpp"
#include "ecs_assert.hpp"
#include "entity.hpp"

#include <concepts>
#include <cstddef>
#include <flat_map>
#include <memory>
#include <span>
#include <utility>

namespace byte::ecs {

/// @brief One SoA of equal-length columns. Always contains EntityID.
template <typename Alloc_ = std::allocator<std::byte>>
class Archetype {
public:
  using allocator_type = Alloc_;
  using size_type = std::size_t;

private:
  std::size_t size_{};
  std::size_t capacity_{};
  [[no_unique_address]] Alloc_ alloc_{};
  std::flat_map<TypeID, Column> columns_{};

public:
  Archetype() {
    add<EntityID>();
  }

  explicit Archetype(const Alloc_& alloc) : alloc_(alloc) {
    add<EntityID>();
  }

  Archetype(const Archetype&) = delete;
  Archetype& operator=(const Archetype&) = delete;

  Archetype(Archetype&& other) noexcept
      : size_(other.size_),
        capacity_(other.capacity_),
        alloc_(std::move(other.alloc_)),
        columns_(std::move(other.columns_)) {
    other.size_ = 0;
    other.capacity_ = 0;
  }

  Archetype& operator=(Archetype&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    destroy_all();
    size_ = other.size_;
    capacity_ = other.capacity_;
    alloc_ = std::move(other.alloc_);
    columns_ = std::move(other.columns_);
    other.size_ = 0;
    other.capacity_ = 0;
    return *this;
  }

  ~Archetype() {
    destroy_all();
  }

  template <typename... Types_>
  [[nodiscard]] static Archetype of(const Alloc_& alloc = Alloc_{}) {
    static_assert((!std::same_as<Types_, EntityID> && ...), "of: entity id is already a column");
    Archetype archetype(alloc);
    (archetype.template add<Types_>(), ...);
    return archetype;
  }

  [[nodiscard]] std::size_t size() const noexcept {
    return size_;
  }

  [[nodiscard]] bool empty() const noexcept {
    return size_ == 0;
  }

  [[nodiscard]] std::size_t capacity() const noexcept {
    return capacity_;
  }

  [[nodiscard]] std::size_t column_count() const noexcept {
    return columns_.size();
  }

  [[nodiscard]] Alloc_ get_allocator() const noexcept {
    return alloc_;
  }

  [[nodiscard]] std::span<const TypeID> signature() const noexcept {
    return columns_.keys();
  }

  [[nodiscard]] Archetype clone_empty() const {
    Archetype out(alloc_);
    for (const auto& [id, col] : columns_) {
      if (id == byte::type_id_v<EntityID>) {
        continue;
      }
      Column copy;
      copy.info = col.info;
      out.columns_.emplace(id, copy);
    }
    return out;
  }

  void reserve(std::size_t n) {
    if (n > capacity_) {
      grow_to(n);
    }
  }

  template <typename Type_>
  void add() {
    add(component_info<Type_>());
  }

  void add(const ComponentInfo& info) {
    const TypeID id = info.id;
    check(!columns_.contains(id), "add: column already present");

    Column col;
    col.info = &info;

    if (size_ > 0 && capacity_ == 0) {
      capacity_ = size_;
    }
    if (capacity_ > 0) {
      auto bytes = byte_alloc();
      col.allocate(bytes, capacity_);
      if (size_ > 0) {
        if (!info.default_constructible()) {
          col.deallocate(bytes);
          fail("add: type is not default constructible");
        }
        for (std::size_t i = 0; i < size_; ++i) {
          col.default_construct_slot(i);
        }
      }
    }

    columns_.emplace(id, col);
    check_buffers();
  }

  template <typename Type_>
  void remove() {
    remove(byte::type_id_v<Type_>);
  }

  void remove(TypeID id) {
    if (id == byte::type_id_v<EntityID>) {
      fail("remove: cannot remove entity id");
    }
    auto it = columns_.find(id);
    check(it != columns_.end(), "remove: missing column");
    auto bytes = byte_alloc();
    it->second.destroy_range(size_);
    it->second.deallocate(bytes);
    columns_.erase(it);
  }

  [[nodiscard]] bool contains(TypeID id) const noexcept {
    return columns_.contains(id);
  }

  template <typename Type_>
  [[nodiscard]] bool contains() const noexcept {
    return contains(byte::type_id_v<Type_>);
  }

  template <typename Type_>
  [[nodiscard]] std::span<Type_> column() {
    Column& col = raw_column(byte::type_id_v<Type_>);
    check(col.info->id == byte::type_id_v<Type_>, "column: type id mismatch");
    return {reinterpret_cast<Type_*>(col.data), size_};
  }

  template <typename Type_>
  [[nodiscard]] std::span<const Type_> column() const {
    const Column& col = raw_column(byte::type_id_v<Type_>);
    check(col.info->id == byte::type_id_v<Type_>, "column: type id mismatch");
    return {reinterpret_cast<const Type_*>(col.data), size_};
  }

  [[nodiscard]] std::span<EntityID> entities() {
    return column<EntityID>();
  }

  [[nodiscard]] std::span<const EntityID> entities() const {
    return column<EntityID>();
  }

  [[nodiscard]] Column& raw_column(TypeID id) {
    auto it = columns_.find(id);
    check(it != columns_.end(), "raw_column: missing column");
    return it->second;
  }

  [[nodiscard]] const Column& raw_column(TypeID id) const {
    auto it = columns_.find(id);
    check(it != columns_.end(), "raw_column: missing column");
    return it->second;
  }

  template <typename Type_>
  [[nodiscard]] Type_& at(std::size_t row) {
    check(row < size_, "at: row out of range");
    return column<Type_>()[row];
  }

  template <typename Type_>
  [[nodiscard]] const Type_& at(std::size_t row) const {
    check(row < size_, "at: row out of range");
    return column<Type_>()[row];
  }

  template <typename... Types_>
  [[nodiscard]] ArchetypeView<Archetype, Types_...> view() {
    return ArchetypeView<Archetype, Types_...>{*this};
  }

  template <typename... Types_>
  [[nodiscard]] ArchetypeView<const Archetype, Types_...> view() const {
    return ArchetypeView<const Archetype, Types_...>{*this};
  }

  /// @brief Append a row. Writes id; other columns default-construct or stay uninit.
  std::size_t push(EntityID id) {
    if (size_ == capacity_) {
      reserve(next_capacity(size_ + 1));
    }
    for (auto it = columns_.begin(); it != columns_.end(); ++it) {
      if (it->first == byte::type_id_v<EntityID>) {
        *reinterpret_cast<EntityID*>(it->second.slot(size_)) = id;
      } else {
        it->second.default_construct_slot(size_);
      }
    }
    check_buffers();
    return size_++;
  }

  /// @brief Returns the entity id that was last (now at row, or the removed id).
  EntityID swap_remove_row(std::size_t row) {
    check(row < size_, "swap_remove_row: row out of range");
    const EntityID last = column<EntityID>()[size_ - 1];
    for (auto it = columns_.begin(); it != columns_.end(); ++it) {
      it->second.swap_remove(row, size_);
    }
    --size_;
    return last;
  }

  template <typename DstAlloc_>
  EntityID migrate_row(std::size_t row, Archetype<DstAlloc_>& dst, TypeID skip_column = {}) {
    check(static_cast<const void*>(this) != static_cast<const void*>(&dst), "migrate_row: same archetype");
    check(row < size_, "migrate_row: row out of range");

    dst.reserve(dst.next_capacity(dst.size_ + 1));
    for (auto it = dst.columns_.begin(); it != dst.columns_.end(); ++it) {
      if (it->first == skip_column) {
        continue;
      }
      auto src = columns_.find(it->first);
      if (src != columns_.end()) {
        it->second.move_construct_slot(dst.size_, src->second, row);
      } else {
        it->second.default_construct_slot(dst.size_);
      }
    }
    ++dst.size_;
    dst.check_buffers();
    return swap_remove_row(row);
  }

private:
  template <typename>
  friend class Archetype;

  using ByteAlloc_ = typename std::allocator_traits<Alloc_>::template rebind_alloc<std::byte>;

  [[nodiscard]] ByteAlloc_ byte_alloc() const {
    return ByteAlloc_(alloc_);
  }

  [[nodiscard]] std::size_t next_capacity(std::size_t needed) const {
    std::size_t cap = capacity_ == 0 ? 1 : capacity_;
    while (cap < needed) {
      cap *= 2;
    }
    return cap;
  }

  void grow_to(std::size_t new_cap) {
    auto bytes = byte_alloc();
    for (auto it = columns_.begin(); it != columns_.end(); ++it) {
      Column neu;
      neu.info = it->second.info;
      neu.allocate(bytes, new_cap);
      neu.relocate_range(it->second, size_);
      it->second.deallocate(bytes);
      it->second = neu;
    }
    capacity_ = new_cap;
    check_buffers();
  }

  void destroy_all() {
    auto bytes = byte_alloc();
    for (auto it = columns_.begin(); it != columns_.end(); ++it) {
      it->second.destroy_range(size_);
      it->second.deallocate(bytes);
    }
    columns_.clear();
    size_ = 0;
    capacity_ = 0;
  }

  void check_buffers() const {
    if (capacity_ == 0) {
      return;
    }
    for (auto it = columns_.begin(); it != columns_.end(); ++it) {
      check(it->second.data != nullptr, "archetype: column buffer is null");
    }
  }
};

template <typename... Types_, typename Alloc_ = std::allocator<std::byte>>
[[nodiscard]] Archetype<Alloc_> make_archetype(const Alloc_& alloc = Alloc_{}) {
  return Archetype<Alloc_>::template of<Types_...>(alloc);
}

} // namespace byte::ecs
