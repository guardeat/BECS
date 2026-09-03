#pragma once

#include "archetype.hpp"
#include "archeview.hpp"
#include "bulk.hpp"
#include "component_info.hpp"
#include "defer.hpp"
#include "entity.hpp"
#include "pool.hpp"
#include "poolview.hpp"
#include "type_id.hpp"

#include <memory>

/// Convenience aliases in namespace byte. Include this header and use `-Isrc`.
namespace byte {

using EntityID = ecs::EntityID;

template <typename Type>
inline constexpr TypeID TypeIdV = type_id_v<Type>;

template <typename Alloc = std::allocator<std::byte>>
using Pool = ecs::Pool<Alloc>;

template <typename Alloc = std::allocator<std::byte>>
using Archetype = ecs::Archetype<Alloc>;

template <typename Pool_>
using Bulk = ecs::Bulk<Pool_>;

template <typename Pool_>
using Defer = ecs::Defer<Pool_>;

template <typename Archetype_, typename... Types>
using PoolView = ecs::PoolView<Archetype_, Types...>;

template <typename Archetype_, typename... Types>
using ArchetypeView = ecs::ArchetypeView<Archetype_, Types...>;

using ComponentInfo = ecs::ComponentInfo;

template <typename Type>
inline constexpr ecs::ComponentInfo ComponentInfoV = ecs::component_info_v<Type>;

template <typename... Types, typename Alloc = std::allocator<std::byte>>
[[nodiscard]] ecs::Archetype<Alloc> MakeArchetype(const Alloc& alloc = Alloc{}) {
  return ecs::make_archetype<Types...>(alloc);
}

} // namespace byte
