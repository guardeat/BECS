#pragma once

#include "ecs_assert.hpp"
#include "entity.hpp"
#include "type_id.hpp"

#include <concepts>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace byte::ecs {

/// @brief Many of the same structural op, now. Holds a Pool&; does not own storage.
template <typename Pool_>
class Bulk {
public:
  using pool_type = Pool_;
  using size_type = typename Pool_::size_type;
  using archetype_type = typename Pool_::archetype_type;

private:
  Pool_& pool_;

public:
  explicit Bulk(Pool_& pool) : pool_(pool) {}

  [[nodiscard]] std::vector<EntityID> create(size_type n) {
    const TypeID ids[] = {byte::type_id_v<EntityID>};
    archetype_type* dst = pool_.ensure(ids, [&] { return archetype_type(pool_.get_allocator()); });
    return spawn_n(dst, n);
  }

  template <typename... Types_>
  [[nodiscard]] std::vector<EntityID> create(size_type n) {
    static_assert(sizeof...(Types_) >= 1, "create: use create() for an empty entity");
    static_assert((!std::same_as<Types_, EntityID> && ...),
                  "create: entity id is already a column");
    const TypeID ids[] = {byte::type_id_v<EntityID>, byte::type_id_v<Types_>...};
    archetype_type* dst = pool_.ensure(
        ids, [&] { return archetype_type::template of<Types_...>(pool_.get_allocator()); });
    return spawn_n(dst, n);
  }

  template <typename Type_>
  void attach(std::span<const EntityID> ids) {
    do_attach<Type_>(ids, nullptr);
  }

  template <typename Type_>
  void attach(std::span<const EntityID> ids, Type_ value) {
    do_attach<Type_>(ids, &value);
  }

  template <typename Type_, typename... Args_>
  void emplace(std::span<const EntityID> ids, Args_&&... args) {
    static_assert(!std::same_as<Type_, EntityID>, "bulk: cannot emplace entity id");
    Type_ value(std::forward<Args_>(args)...);
    attach<Type_>(ids, std::move(value));
  }

  template <typename Type_, typename Fn_>
  void emplace_with(std::span<const EntityID> ids, Fn_&& fn) {
    static_assert(!std::same_as<Type_, EntityID>, "bulk: cannot emplace entity id");

    for (auto& [src, group] : group_by_source(ids)) {
      if (src->template contains<Type_>()) {
        check(false, "emplace_with: component already present");
        continue;
      }

      archetype_type* dst = pool_.template transition_add<Type_>(src);
      dst->reserve(dst->size() + group.size());
      for (EntityID id : group) {
        pool_.relocate(id, pool_.location(id), dst, byte::type_id_v<Type_>);
        auto& loc = pool_.location(id);
        Type_* slot = reinterpret_cast<Type_*>(loc.archetype->raw_column(byte::type_id_v<Type_>).slot(loc.row));
        if constexpr (std::is_invocable_v<Fn_, EntityID>) {
          std::construct_at(slot, fn(id));
        } else {
          std::construct_at(slot, fn());
        }
      }
    }
  }

  template <typename Type_>
  void detach(std::span<const EntityID> ids) {
    static_assert(!std::same_as<Type_, EntityID>, "bulk: cannot detach entity id");

    for (auto& [src, group] : group_by_source(ids)) {
      if (!src->template contains<Type_>()) {
        check(false, "detach: missing component");
        continue;
      }

      archetype_type* dst = pool_.template transition_remove<Type_>(src);
      dst->reserve(dst->size() + group.size());
      for (EntityID id : group) {
        pool_.relocate(id, pool_.location(id), dst);
      }
    }
  }

  void destroy(std::span<const EntityID> ids) {
    for (EntityID id : ids) {
      pool_.destroy(id);
    }
  }

private:
  [[nodiscard]] std::vector<EntityID> spawn_n(archetype_type* dst, size_type n) {
    pool_.reserve(pool_.size() + n);
    dst->reserve(dst->size() + n);
    std::vector<EntityID> out;
    out.reserve(n);
    for (size_type i = 0; i < n; ++i) {
      out.push_back(pool_.spawn(dst));
    }
    return out;
  }

  [[nodiscard]] std::unordered_map<archetype_type*, std::vector<EntityID>>
  group_by_source(std::span<const EntityID> ids) {
    std::unordered_map<archetype_type*, std::vector<EntityID>> groups;
    for (EntityID id : ids) {
      if (!pool_.contains(id)) {
        check(false, "bulk: invalid entity");
        continue;
      }
      groups[pool_.location(id).archetype].push_back(id);
    }
    return groups;
  }

  template <typename Type_>
  void do_attach(std::span<const EntityID> ids, const Type_* value) {
    static_assert(!std::same_as<Type_, EntityID>, "bulk: cannot attach entity id");

    for (auto& [src, group] : group_by_source(ids)) {
      if (src->template contains<Type_>()) {
        check(false, "attach: component already present");
        continue;
      }

      archetype_type* dst = pool_.template transition_add<Type_>(src);
      dst->reserve(dst->size() + group.size());
      for (EntityID id : group) {
        if (value != nullptr) {
          pool_.relocate(id, pool_.location(id), dst, byte::type_id_v<Type_>);
          auto& loc = pool_.location(id);
          Type_* slot = reinterpret_cast<Type_*>(loc.archetype->raw_column(byte::type_id_v<Type_>).slot(loc.row));
          std::construct_at(slot, *value);
        } else {
          pool_.relocate(id, pool_.location(id), dst);
        }
      }
    }
  }
};

} // namespace byte::ecs
