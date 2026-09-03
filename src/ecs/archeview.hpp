#pragma once

#include "ecs_assert.hpp"

#include <cstddef>
#include <iterator>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace byte::ecs {

/// @brief Non-owning typed view. Binds T* once; reads are ptr[row].
template <typename Archetype_, typename... Types_>
class ArchetypeView {
  static_assert(sizeof...(Types_) >= 1, "ArchetypeView: at least one type");

public:
  using archetype_type = Archetype_;
  using size_type = std::size_t;
  static constexpr bool is_const = std::is_const_v<std::remove_reference_t<Archetype_>>;
  using row_type = std::tuple<std::conditional_t<is_const, const Types_&, Types_&>...>;

  class iterator {
  public:
    using iterator_concept = std::random_access_iterator_tag;
    using iterator_category = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = std::tuple<std::remove_cvref_t<Types_>...>;
    using reference = row_type;
    using pointer = void;

  private:
    const ArchetypeView* view_{};
    std::size_t row_{};

  public:
    iterator() = default;
    iterator(const ArchetypeView& view, std::size_t row) noexcept : view_{&view}, row_{row} {}

    [[nodiscard]] reference operator*() const {
      return view_->at(row_, std::index_sequence_for<Types_...>{});
    }

    [[nodiscard]] reference operator[](difference_type n) const {
      return *(*this + n);
    }

    iterator& operator++() {
      ++row_;
      return *this;
    }

    iterator operator++(int) {
      iterator tmp = *this;
      ++*this;
      return tmp;
    }

    iterator& operator--() {
      --row_;
      return *this;
    }

    iterator operator--(int) {
      iterator tmp = *this;
      --*this;
      return tmp;
    }

    iterator& operator+=(difference_type n) {
      row_ = static_cast<std::size_t>(static_cast<difference_type>(row_) + n);
      return *this;
    }

    iterator& operator-=(difference_type n) {
      return *this += -n;
    }

    [[nodiscard]] friend iterator operator+(iterator it, difference_type n) {
      return it += n;
    }

    [[nodiscard]] friend iterator operator+(difference_type n, iterator it) {
      return it += n;
    }

    [[nodiscard]] friend iterator operator-(iterator it, difference_type n) {
      return it -= n;
    }

    [[nodiscard]] friend difference_type operator-(const iterator& a, const iterator& b) {
      return static_cast<difference_type>(a.row_) - static_cast<difference_type>(b.row_);
    }

    [[nodiscard]] friend bool operator==(const iterator&, const iterator&) = default;

    [[nodiscard]] friend bool operator<(const iterator& a, const iterator& b) {
      return a.row_ < b.row_;
    }

    [[nodiscard]] friend bool operator>(const iterator& a, const iterator& b) {
      return b < a;
    }

    [[nodiscard]] friend bool operator<=(const iterator& a, const iterator& b) {
      return !(b < a);
    }

    [[nodiscard]] friend bool operator>=(const iterator& a, const iterator& b) {
      return !(a < b);
    }
  };

private:
  std::size_t size_{};
  std::tuple<std::conditional_t<is_const, const Types_*, Types_*>...> ptrs_{};

public:
  explicit ArchetypeView(Archetype_& arche)
      : size_(arche.size()), ptrs_{arche.template column<Types_>().data()...} {}

  [[nodiscard]] std::size_t size() const noexcept {
    return size_;
  }

  [[nodiscard]] bool empty() const noexcept {
    return size_ == 0;
  }

  [[nodiscard]] row_type operator[](std::size_t row) const {
    check(row < size_, "ArchetypeView: row out of range");
    return at(row, std::index_sequence_for<Types_...>{});
  }

  template <typename Type_>
  [[nodiscard]] auto get() const {
    using Ptr = std::conditional_t<is_const, const Type_*, Type_*>;
    return std::span{std::get<Ptr>(ptrs_), size_};
  }

  [[nodiscard]] iterator begin() const noexcept {
    return {*this, 0};
  }

  [[nodiscard]] iterator end() const noexcept {
    return {*this, size_};
  }

private:
  template <std::size_t... Index_>
  [[nodiscard]] row_type at(std::size_t row, std::index_sequence<Index_...>) const {
    return row_type{std::get<Index_>(ptrs_)[row]...};
  }
};

} // namespace byte::ecs
