#pragma once

#include "component_info.hpp"
#include "entity.hpp"
#include "type_id.hpp"

#include <concepts>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace byte::ecs {

namespace detail {

struct AttachCmd {
public:
  EntityID id;
  const ComponentInfo* info;
  std::size_t offset;
  bool has_value;
};

struct DetachCmd {
public:
  EntityID id;
  TypeID type_id;
};

struct DeferQueue {
public:
  std::vector<EntityID> destroys{};
  std::vector<DetachCmd> detaches{};
  std::vector<AttachCmd> attaches{};
  std::vector<std::byte> payload{};

  [[nodiscard]] bool empty() const noexcept {
    return destroys.empty() && detaches.empty() && attaches.empty();
  }

  [[nodiscard]] std::size_t size() const noexcept {
    return destroys.size() + detaches.size() + attaches.size();
  }

  void clear() noexcept {
    for (const auto& cmd : attaches) {
      if (cmd.has_value && cmd.info != nullptr && cmd.info->destroy != nullptr) {
        cmd.info->destroy(&payload[cmd.offset]);
      }
    }
    destroys.clear();
    detaches.clear();
    attaches.clear();
    payload.clear();
  }

  ~DeferQueue() { clear(); }

  DeferQueue() = default;
  DeferQueue(const DeferQueue&) = delete;
  DeferQueue& operator=(const DeferQueue&) = delete;
  DeferQueue(DeferQueue&& other) noexcept
      : destroys(std::move(other.destroys)), detaches(std::move(other.detaches)),
        attaches(std::move(other.attaches)), payload(std::move(other.payload)) {}

  DeferQueue& operator=(DeferQueue&& other) noexcept {
    if (this != &other) {
      clear();
      destroys = std::move(other.destroys);
      detaches = std::move(other.detaches);
      attaches = std::move(other.attaches);
      payload = std::move(other.payload);
    }
    return *this;
  }
};

} // namespace detail

/// @brief Deferred structural operations (attach, detach, destroy) queued for batch execution.
template <typename Pool_>
class Defer {
public:
  using pool_type = Pool_;
  using size_type = typename Pool_::size_type;
  using archetype_type = typename Pool_::archetype_type;

private:
  Pool_& pool_;
  detail::DeferQueue* queue_ = nullptr;
  std::unique_ptr<detail::DeferQueue> owned_queue_{};

  detail::DeferQueue& queue() noexcept { return *queue_; }

  const detail::DeferQueue& queue() const noexcept { return *queue_; }

public:
  explicit Defer(Pool_& pool, detail::DeferQueue* queue = nullptr) : pool_(pool), queue_(queue) {
    if (queue_ == nullptr) {
      owned_queue_ = std::make_unique<detail::DeferQueue>();
      queue_ = owned_queue_.get();
    }
  }

  template <typename Type_, typename... Args_>
  void emplace(EntityID id, Args_&&... args) {
    static_assert(!std::same_as<Type_, EntityID>, "defer: cannot emplace entity id");
    const ComponentInfo& info = component_info_v<Type_>;
    auto& q = queue();

    std::size_t offset = q.payload.size();
    if (const std::size_t rem = offset % info.alignment; rem != 0) {
      offset += (info.alignment - rem);
    }
    q.payload.resize(offset + info.size);

    std::construct_at(reinterpret_cast<Type_*>(&q.payload[offset]), std::forward<Args_>(args)...);

    q.attaches.push_back(detail::AttachCmd{
        .id = id,
        .info = &info,
        .offset = offset,
        .has_value = true,
    });
  }

  template <typename Type_>
  void attach(EntityID id, Type_ value) {
    emplace<Type_>(id, std::move(value));
  }

  template <typename Type_>
  void attach(EntityID id) {
    static_assert(!std::same_as<Type_, EntityID>, "defer: cannot attach entity id");
    const ComponentInfo& info = component_info_v<Type_>;
    queue().attaches.push_back(detail::AttachCmd{
        .id = id,
        .info = &info,
        .offset = 0,
        .has_value = false,
    });
  }

  template <typename Type_>
  void detach(EntityID id) {
    static_assert(!std::same_as<Type_, EntityID>, "defer: cannot detach entity id");
    queue().detaches.push_back(detail::DetachCmd{
        .id = id,
        .type_id = byte::type_id_v<Type_>,
    });
  }

  void destroy(EntityID id) { queue().destroys.push_back(id); }

  [[nodiscard]] bool empty() const noexcept { return queue().empty(); }

  [[nodiscard]] std::size_t size() const noexcept { return queue().size(); }

  void clear() noexcept { queue().clear(); }

  /// @brief Executes all queued operations in batch order (detaches -> attaches -> destroys).
  void flush() {
    auto& q = queue();
    if (q.empty()) {
      return;
    }

    // Phase 1: Detaches
    for (const auto& cmd : q.detaches) {
      if (!pool_.contains(cmd.id)) {
        continue;
      }
      auto& loc = pool_.location(cmd.id);
      if (!loc.archetype->contains(cmd.type_id)) {
        continue;
      }
      archetype_type* dst = pool_.transition_remove(loc.archetype, cmd.type_id);
      pool_.relocate(cmd.id, loc, dst);
    }

    // Phase 2: Attaches
    for (const auto& cmd : q.attaches) {
      std::byte* src = cmd.has_value ? &q.payload[cmd.offset] : nullptr;

      if (!pool_.contains(cmd.id)) {
        if (cmd.has_value && cmd.info->destroy != nullptr) {
          cmd.info->destroy(src);
        }
        continue;
      }

      auto& loc = pool_.location(cmd.id);
      if (loc.archetype->contains(cmd.info->id)) {
        if (cmd.has_value && cmd.info->destroy != nullptr) {
          cmd.info->destroy(src);
        }
        continue;
      }

      archetype_type* dst = pool_.transition_add(loc.archetype, *cmd.info);
      pool_.relocate(cmd.id, loc, dst, cmd.has_value ? cmd.info->id : TypeID{});

      auto& loc_new = pool_.location(cmd.id);
      std::byte* slot = loc_new.archetype->raw_column(cmd.info->id).slot(loc_new.row);

      if (cmd.has_value) {
        if (cmd.info->move_construct != nullptr) {
          cmd.info->move_construct(slot, src);
        } else {
          std::memcpy(slot, src, cmd.info->size);
        }
        if (cmd.info->destroy != nullptr) {
          cmd.info->destroy(src);
        }
      }
    }

    // Phase 3: Destroys
    for (EntityID id : q.destroys) {
      if (pool_.contains(id)) {
        pool_.destroy(id);
      }
    }

    q.destroys.clear();
    q.detaches.clear();
    q.attaches.clear();
    q.payload.clear();
  }
};

} // namespace byte::ecs
