#pragma once

#include "ecs_assert.hpp"
#include "entity.hpp"
#include "type_id.hpp"

#include <concepts>
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
    archetype_type* dst = pool_.ensure(ids, [&] {
      return archetype_type(pool_.get_allocator());
    });
    return spawn_n(dst, n);
  }

  template <typename... Types_>
  [[nodiscard]] std::vector<EntityID> create(size_type n) {
    static_assert(sizeof...(Types_) >= 1, "create: use create() for an empty entity");
    static_assert((!std::same_as<Types_, EntityID> && ...), "create: entity id is already a column");
    const TypeID ids[] = {byte::type_id_v<EntityID>, byte::type_id_v<Types_>...};
    archetype_type* dst = pool_.ensure(ids, [&] {
      return archetype_type::template of<Types_...>(pool_.get_allocator());
    });
    return spawn_n(dst, n);
  }

  template <typename Type_>
  void attach(std::span<const EntityID> ids) {
    migrate<Type_, true>(ids, nullptr);
  }

  template <typename Type_>
  void attach(std::span<const EntityID> ids, Type_ value) {
    migrate<Type_, true>(ids, &value);
  }

  template <typename Type_>
  void detach(std::span<const EntityID> ids) {
    migrate<Type_, false>(ids, nullptr);
  }

  void destroy(std::span<const EntityID> ids) {
    for (EntityID id : ids) {
      if (pool_.contains(id)) {
        pool_.destroy(id);
      }
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

  template <typename Type_, bool Attach_>
  void migrate(std::span<const EntityID> ids, const Type_* value) {
    static_assert(!std::same_as<Type_, EntityID>, "bulk: cannot attach or detach entity id");

    for (auto& [src, group] : group_by_source(ids)) {
      const bool present = src->template contains<Type_>();
      if constexpr (Attach_) {
        if (present) {
          check(false, "attach: component already present");
          continue;
        }
      } else {
        if (!present) {
          check(false, "detach: missing component");
          continue;
        }
      }

      auto keys = src->signature();
      std::vector<TypeID> sig(keys.begin(), keys.end());
      if constexpr (Attach_) {
        sig.push_back(byte::type_id_v<Type_>);
      } else {
        std::erase(sig, byte::type_id_v<Type_>);
      }

      archetype_type* dst = pool_.ensure(sig, [src] {
        archetype_type next = src->clone_empty();
        if constexpr (Attach_) {
          next.template add<Type_>();
        } else {
          next.template remove<Type_>();
        }
        return next;
      });
      dst->reserve(dst->size() + group.size());
      for (EntityID id : group) {
        pool_.relocate(id, pool_.location(id), dst);
        if constexpr (Attach_) {
          if (value != nullptr) {
            pool_.template get<Type_>(id) = *value;
          }
        }
      }
    }
  }
};

} // namespace byte::ecs
