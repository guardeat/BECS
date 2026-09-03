#pragma once

#include "archetype.hpp"
#include "bulk.hpp"
#include "defer.hpp"
#include "ecs_assert.hpp"
#include "entity.hpp"
#include "poolview.hpp"
#include "inverted_map.hpp"
#include "sparse_vector.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace byte::ecs {

/// @brief Owns archetypes and entity locations. Entity id is the sparse_vector index.
template <typename Alloc_ = std::allocator<std::byte>>
class Pool {
public:
  using allocator_type = Alloc_;
  using size_type = std::size_t;
  using archetype_type = Archetype<Alloc_>;

  struct Location {
    archetype_type* archetype = nullptr;
    size_type row = 0;
  };

private:
  using ArchetypeMap_ = byte::inverted_map<TypeID, archetype_type>;

  [[no_unique_address]] Alloc_ alloc_{};
  ArchetypeMap_ archetypes_{};
  byte::sparse_vector<Location> entities_{};
  detail::DeferQueue defer_queue_{};

public:
  Pool() = default;

  explicit Pool(const Alloc_& alloc) : alloc_(alloc) {}

  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;

  Pool(Pool&& other)
      : alloc_(std::move(other.alloc_)),
        archetypes_(std::move(other.archetypes_)),
        entities_(std::move(other.entities_)),
        defer_queue_(std::move(other.defer_queue_)) {
    rebind();
  }

  Pool& operator=(Pool&& other) {
    if (this != &other) {
      alloc_ = std::move(other.alloc_);
      archetypes_ = std::move(other.archetypes_);
      entities_ = std::move(other.entities_);
      defer_queue_ = std::move(other.defer_queue_);
      rebind();
    }
    return *this;
  }

  [[nodiscard]] Alloc_ get_allocator() const noexcept {
    return alloc_;
  }

  [[nodiscard]] size_type size() const noexcept {
    return entities_.size();
  }

  [[nodiscard]] bool empty() const noexcept {
    return entities_.empty();
  }

  [[nodiscard]] bool contains(EntityID id) const noexcept {
    return entities_.contains(index_of(id));
  }

  void reserve(size_type n) {
    entities_.reserve(n);
  }

  [[nodiscard]] EntityID create() {
    const TypeID ids[] = {byte::type_id_v<EntityID>};
    return spawn(ensure(ids, [this] { return archetype_type(alloc_); }));
  }

  template <typename... Types_>
  [[nodiscard]] EntityID create() {
    static_assert(sizeof...(Types_) >= 1, "create: use create() for an empty entity");
    static_assert((!std::same_as<Types_, EntityID> && ...), "create: entity id is already a column");
    const TypeID ids[] = {byte::type_id_v<EntityID>, byte::type_id_v<Types_>...};
    return spawn(ensure(ids, [this] {
      return archetype_type::template of<Types_...>(alloc_);
    }));
  }

  void destroy(EntityID id) {
    Location& loc = location(id);
    const size_type old_row = loc.row;
    const EntityID last = loc.archetype->swap_remove_row(old_row);
    if (last != id) {
      entities_[index_of(last)].row = old_row;
    }
    entities_.erase(index_of(id));
  }

  [[nodiscard]] archetype_type* transition_add(archetype_type* src, const ComponentInfo& info) {
    auto keys = src->signature();
    std::vector<TypeID> sig(keys.begin(), keys.end());
    sig.push_back(info.id);
    return ensure(sig, [src, &info] {
      archetype_type next = src->clone_empty();
      next.add(info);
      return next;
    });
  }

  template <typename Type_>
  [[nodiscard]] archetype_type* transition_add(archetype_type* src) {
    return transition_add(src, component_info_v<Type_>);
  }

  [[nodiscard]] archetype_type* transition_remove(archetype_type* src, TypeID id) {
    auto keys = src->signature();
    std::vector<TypeID> sig(keys.begin(), keys.end());
    std::erase(sig, id);
    return ensure(sig, [src, id] {
      archetype_type next = src->clone_empty();
      next.remove(id);
      return next;
    });
  }

  template <typename Type_>
  [[nodiscard]] archetype_type* transition_remove(archetype_type* src) {
    return transition_remove(src, byte::type_id_v<Type_>);
  }

  template <typename Type_>
  void attach(EntityID id) {
    static_assert(!std::same_as<Type_, EntityID>, "attach: entity id is already a column");
    Location& loc = location(id);
    if (loc.archetype->template contains<Type_>()) {
      check(false, "attach: component already present");
      return;
    }
    archetype_type* dst = transition_add<Type_>(loc.archetype);
    relocate(id, loc, dst);
  }

  template <typename Type_, typename... Args_>
  Type_& emplace(EntityID id, Args_&&... args) {
    static_assert(!std::same_as<Type_, EntityID>, "emplace: entity id is already a column");
    Location& loc = location(id);
    if (loc.archetype->template contains<Type_>()) {
      check(false, "emplace: component already present");
      return get<Type_>(id);
    }
    archetype_type* dst = transition_add<Type_>(loc.archetype);
    relocate(id, loc, dst, byte::type_id_v<Type_>);
    Type_* ptr = reinterpret_cast<Type_*>(loc.archetype->raw_column(byte::type_id_v<Type_>).slot(loc.row));
    std::construct_at(ptr, std::forward<Args_>(args)...);
    return *ptr;
  }

  template <typename Type_>
  Type_& attach(EntityID id, Type_ value) {
    return emplace<Type_>(id, std::move(value));
  }

  template <typename Type_>
  void detach(EntityID id) {
    static_assert(!std::same_as<Type_, EntityID>, "detach: cannot remove entity id");
    Location& loc = location(id);
    if (!loc.archetype->template contains<Type_>()) {
      check(false, "detach: missing component");
      return;
    }
    archetype_type* dst = transition_remove<Type_>(loc.archetype);
    relocate(id, loc, dst);
  }

  template <typename Type_>
  [[nodiscard]] Type_& get(EntityID id) {
    Location& loc = location(id);
    return loc.archetype->template at<Type_>(loc.row);
  }

  template <typename Type_>
  [[nodiscard]] const Type_& get(EntityID id) const {
    const Location& loc = location(id);
    return loc.archetype->template at<Type_>(loc.row);
  }

  template <typename Type_>
  [[nodiscard]] bool has(EntityID id) const {
    return location(id).archetype->template contains<Type_>();
  }

  template <typename... Types_>
  [[nodiscard]] PoolView<archetype_type, Types_...> view() {
    return make_view<archetype_type, Types_...>(archetypes_.supersets(query<Types_...>()));
  }

  template <typename... Types_>
  [[nodiscard]] PoolView<const archetype_type, Types_...> view() const {
    return make_view<const archetype_type, Types_...>(archetypes_.supersets(query<Types_...>()));
  }

  template <typename... Types_, typename Fn_>
  void each(Fn_&& fn) {
    view<Types_...>().each(std::forward<Fn_>(fn));
  }

  template <typename... Types_, typename Fn_>
  void each(Fn_&& fn) const {
    view<Types_...>().each(std::forward<Fn_>(fn));
  }

  [[nodiscard]] Bulk<Pool> bulk() {
    return Bulk<Pool>{*this};
  }

  [[nodiscard]] Defer<Pool> defer() {
    return Defer<Pool>{*this, &defer_queue_};
  }

  void flush() {
    defer().flush();
  }

  [[nodiscard]] Location& location(EntityID id) {
    const size_type index = index_of(id);
    check(entities_.contains(index), "pool: invalid entity");
    return entities_[index];
  }

  [[nodiscard]] const Location& location(EntityID id) const {
    const size_type index = index_of(id);
    check(entities_.contains(index), "pool: invalid entity");
    return entities_[index];
  }

  template <typename Fn_>
  [[nodiscard]] archetype_type* ensure(std::span<const TypeID> sig, Fn_&& fn) {
    if (auto it = archetypes_.find(sig); it != archetypes_.end()) {
      return &*it;
    }
    auto [it, inserted] = archetypes_.insert(sig, std::forward<Fn_>(fn)());
    (void)inserted;
    return &*it;
  }

  [[nodiscard]] EntityID spawn(archetype_type* archetype) {
    const size_type index = entities_.emplace(Location{});
    const EntityID id = id_of(index);
    const size_type row = archetype->push(id);
    entities_[index] = Location{archetype, row};
    return id;
  }

  void relocate(EntityID id, Location& loc, archetype_type* dst, TypeID skip_column = {}) {
    if (loc.archetype == dst) {
      return;
    }
    const size_type old_row = loc.row;
    const EntityID last = loc.archetype->migrate_row(old_row, *dst, skip_column);
    if (last != id) {
      entities_[index_of(last)].row = old_row;
    }
    loc.archetype = dst;
    loc.row = dst->size() - 1;
  }

private:
  template <typename... Types_>
  [[nodiscard]] static std::span<const TypeID> query() {
    static const TypeID ids[] = {byte::type_id_v<Types_>...};
    return ids;
  }

  template <typename Archetype_, typename... Types_>
  [[nodiscard]] static PoolView<Archetype_, Types_...> make_view(std::vector<Archetype_*> archetypes) {
    return PoolView<Archetype_, Types_...>{archetypes};
  }

  [[nodiscard]] static size_type index_of(EntityID id) noexcept {
    return static_cast<size_type>(id.value);
  }

  [[nodiscard]] static EntityID id_of(size_type index) noexcept {
    return EntityID(static_cast<std::uint64_t>(index));
  }

  void rebind() {
    for (archetype_type& archetype : archetypes_) {
      auto ids = archetype.entities();
      for (size_type row = 0; row < archetype.size(); ++row) {
        entities_[index_of(ids[row])] = Location{&archetype, row};
      }
    }
  }
};

} // namespace byte::ecs
