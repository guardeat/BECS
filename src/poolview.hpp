#pragma once

#include "archeview.hpp"
#include "entity.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace byte::ecs {

/// @brief Concatenation of ArchetypeViews. Binds T* once per matching archetype.
/// `.with<T>()` / `.without<T>()` filter archetypes; T is not added to the row tuple.
template <typename Archetype_, typename... Types_>
class PoolView {
  static_assert(sizeof...(Types_) >= 1, "PoolView: at least one type");

public:
  using archetype_type = Archetype_;
  using chunk_type = ArchetypeView<Archetype_, Types_...>;
  using size_type = std::size_t;
  using row_type = typename chunk_type::row_type;

  class iterator {
  public:
    using iterator_concept = std::forward_iterator_tag;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = typename chunk_type::iterator::value_type;
    using reference = row_type;
    using pointer = void;

  private:
    const chunk_type* chunk_{};
    const chunk_type* chunks_end_{};
    typename chunk_type::iterator inner_{};

  public:
    iterator() = default;

    iterator(const chunk_type* chunk, const chunk_type* chunks_end, typename chunk_type::iterator inner) noexcept
        : chunk_{chunk}, chunks_end_{chunks_end}, inner_{inner} {}

    [[nodiscard]] reference operator*() const {
      return *inner_;
    }

    iterator& operator++() {
      ++inner_;
      if (inner_ == chunk_->end()) {
        ++chunk_;
        if (chunk_ != chunks_end_) {
          inner_ = chunk_->begin();
        }
      }
      return *this;
    }

    iterator operator++(int) {
      iterator tmp = *this;
      ++*this;
      return tmp;
    }

    [[nodiscard]] bool operator==(const iterator& other) const {
      return chunk_ == other.chunk_ && (chunk_ == chunks_end_ || inner_ == other.inner_);
    }
  };

private:
  std::vector<Archetype_*> archetypes_{};
  std::vector<chunk_type> chunks_{};
  size_type size_{};

public:
  explicit PoolView(std::span<Archetype_* const> archetypes)
      : archetypes_(archetypes.begin(), archetypes.end()) {
    rebuild();
  }

  template <typename... WithTypes_>
  [[nodiscard]] PoolView with() & {
    static_assert(sizeof...(WithTypes_) >= 1, "with: at least one type");
    PoolView copy = *this;
    return std::move(copy).template with<WithTypes_...>();
  }

  template <typename... WithTypes_>
  [[nodiscard]] PoolView with() && {
    static_assert(sizeof...(WithTypes_) >= 1, "with: at least one type");
    auto reject = [](Archetype_* archetype) {
      return (!archetype->template contains<WithTypes_>() || ...);
    };
    archetypes_.erase(std::remove_if(archetypes_.begin(), archetypes_.end(), reject), archetypes_.end());
    rebuild();
    return std::move(*this);
  }

  template <typename... WithoutTypes_>
  [[nodiscard]] PoolView without() & {
    static_assert(sizeof...(WithoutTypes_) >= 1, "without: at least one type");
    static_assert((!std::same_as<WithoutTypes_, EntityID> && ...), "without: cannot exclude EntityID");
    PoolView copy = *this;
    return std::move(copy).template without<WithoutTypes_...>();
  }

  template <typename... WithoutTypes_>
  [[nodiscard]] PoolView without() && {
    static_assert(sizeof...(WithoutTypes_) >= 1, "without: at least one type");
    static_assert((!std::same_as<WithoutTypes_, EntityID> && ...), "without: cannot exclude EntityID");
    auto reject = [](Archetype_* archetype) {
      return (archetype->template contains<WithoutTypes_>() || ...);
    };
    archetypes_.erase(std::remove_if(archetypes_.begin(), archetypes_.end(), reject), archetypes_.end());
    rebuild();
    return std::move(*this);
  }

  [[nodiscard]] size_type size() const noexcept {
    return size_;
  }

  [[nodiscard]] bool empty() const noexcept {
    return size_ == 0;
  }

  template <typename Fn_>
  void each(Fn_&& fn) const {
    for (const chunk_type& chunk : chunks_) {
      apply(chunk, fn, std::index_sequence_for<Types_...>{});
    }
  }

  [[nodiscard]] iterator begin() const {
    if (chunks_.empty()) {
      return {};
    }
    const chunk_type* data = chunks_.data();
    return {data, data + chunks_.size(), data->begin()};
  }

  [[nodiscard]] iterator end() const {
    if (chunks_.empty()) {
      return {};
    }
    const chunk_type* data = chunks_.data();
    const chunk_type* last = data + chunks_.size();
    return {last, last, {}};
  }

private:
  void rebuild() {
    chunks_.clear();
    size_ = 0;
    chunks_.reserve(archetypes_.size());
    for (Archetype_* archetype : archetypes_) {
      if (archetype->size() == 0) {
        continue;
      }
      chunks_.emplace_back(*archetype);
      size_ += chunks_.back().size();
    }
  }

  template <typename Fn_, std::size_t... Index_>
  static void apply(const chunk_type& chunk, Fn_& fn, std::index_sequence<Index_...>) {
    const size_type n = chunk.size();
    auto ptrs = std::tuple{chunk.template get<Types_>().data()...};
    for (size_type i = 0; i < n; ++i) {
      fn(std::get<Index_>(ptrs)[i]...);
    }
  }
};

} // namespace byte::ecs
