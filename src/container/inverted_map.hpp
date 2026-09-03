#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace byte {

/// @brief Map from sets (stored as canonical sorted vectors) to values,
/// with inverted-index posting lists for fast superset queries.
template <
    typename Element_,
    typename Value_,
    typename ElementHash_ = std::hash<Element_>,
    typename ElementEqual_ = std::equal_to<Element_>,
    typename ElementLess_ = std::less<Element_>
>
class inverted_map {
public:
  using element_type = Element_;
  using mapped_type = Value_;
  using value_type = Value_;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using key_type = std::vector<Element_>;
  using hasher = ElementHash_;
  using key_equal = ElementEqual_;
  using key_compare = ElementLess_;

private:
  struct internal_key_hash {
  public:
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(const key_type& key) const noexcept {
      return hash_span(std::span<const Element_>{key});
    }

    [[nodiscard]] std::size_t operator()(std::span<const Element_> key) const noexcept {
      return hash_span(key);
    }

  private:
    [[nodiscard]] static std::size_t hash_span(std::span<const Element_> key) noexcept {
      // 64-bit golden ratio hash combine
      std::size_t seed = 0xcbf29ce484222325ULL;
      ElementHash_ elem_hasher{};
      for (const Element_& elem : key) {
        std::size_t h = elem_hasher(elem);
        seed ^= h + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
      }
      return seed;
    }
  };

  struct internal_key_equal {
  public:
    using is_transparent = void;

    [[nodiscard]] bool operator()(const key_type& lhs, const key_type& rhs) const noexcept {
      return std::ranges::equal(lhs, rhs, ElementEqual_{});
    }
    [[nodiscard]] bool operator()(const key_type& lhs, std::span<const Element_> rhs) const noexcept {
      return std::ranges::equal(lhs, rhs, ElementEqual_{});
    }
    [[nodiscard]] bool operator()(std::span<const Element_> lhs, const key_type& rhs) const noexcept {
      return std::ranges::equal(lhs, rhs, ElementEqual_{});
    }
    [[nodiscard]] bool operator()(std::span<const Element_> lhs, std::span<const Element_> rhs) const noexcept {
      return std::ranges::equal(lhs, rhs, ElementEqual_{});
    }
  };

  /// @brief Internal helper to normalize (sort + deduplicate) an element span without SBO.
  struct canonical_key {
  public:
    explicit canonical_key(std::span<const Element_> input) {
      if (input.empty()) {
        view_ = {};
        return;
      }

      // Fast-path: already strictly sorted without adjacent duplicates
      if (std::is_sorted(input.begin(), input.end(), ElementLess_{}) &&
          std::adjacent_find(input.begin(), input.end(), ElementEqual_{}) == input.end()) {
        view_ = input;
        return;
      }

      storage_.assign(input.begin(), input.end());
      std::sort(storage_.begin(), storage_.end(), ElementLess_{});
      storage_.erase(std::unique(storage_.begin(), storage_.end(), ElementEqual_{}), storage_.end());
      view_ = storage_;
    }

    [[nodiscard]] std::span<const Element_> span() const noexcept {
      return view_;
    }

    [[nodiscard]] std::vector<Element_> to_vector() const {
      if (!storage_.empty()) {
        return storage_;
      }
      return {view_.begin(), view_.end()};
    }

  private:
    std::vector<Element_> storage_{};
    std::span<const Element_> view_{};
  };

  using norm_helper = canonical_key;
  using map_type = std::unordered_map<key_type, Value_, internal_key_hash, internal_key_equal>;
  using entry = typename map_type::value_type;

  map_type nodes_{};
  std::unordered_map<Element_, std::vector<entry*>, ElementHash_, ElementEqual_> invert_{};

public:
  template <bool IsConst_>
  class basic_iterator {
  public:
    using iterator_concept = std::forward_iterator_tag;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Value_;
    using reference = std::conditional_t<IsConst_, const Value_&, Value_&>;
    using pointer = std::conditional_t<IsConst_, const Value_*, Value_*>;

  private:
    using base_iter = std::conditional_t<IsConst_,
        typename map_type::const_iterator,
        typename map_type::iterator>;
    base_iter it_{};

  public:
    basic_iterator() = default;
    explicit basic_iterator(base_iter it) noexcept : it_(it) {}

    template <bool OtherConst_>
      requires (IsConst_ && !OtherConst_)
    basic_iterator(const basic_iterator<OtherConst_>& other) noexcept : it_(other.base()) {}

    [[nodiscard]] reference operator*() const noexcept {
      return it_->second;
    }

    [[nodiscard]] pointer operator->() const noexcept {
      return &it_->second;
    }

    basic_iterator& operator++() noexcept {
      ++it_;
      return *this;
    }

    basic_iterator operator++(int) noexcept {
      basic_iterator tmp = *this;
      ++it_;
      return tmp;
    }

    template <bool OtherConst_>
    [[nodiscard]] bool operator==(const basic_iterator<OtherConst_>& other) const noexcept {
      return it_ == other.base();
    }

    [[nodiscard]] base_iter base() const noexcept {
      return it_;
    }
  };

  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;

  inverted_map() = default;

  inverted_map(const inverted_map& other)
    requires std::copy_constructible<Value_>
      : nodes_(other.nodes_) {
    reindex();
  }

  inverted_map& operator=(const inverted_map& other)
    requires std::copy_constructible<Value_>
  {
    if (this != &other) {
      inverted_map tmp(other);
      *this = std::move(tmp);
    }
    return *this;
  }

  inverted_map(inverted_map&& other) noexcept
      : nodes_(std::move(other.nodes_)), invert_(std::move(other.invert_)) {}

  inverted_map& operator=(inverted_map&& other) noexcept {
    if (this != &other) {
      nodes_ = std::move(other.nodes_);
      invert_ = std::move(other.invert_);
    }
    return *this;
  }

  [[nodiscard]] hasher hash_function() const {
    return hasher{};
  }

  [[nodiscard]] key_equal key_eq() const {
    return key_equal{};
  }

  [[nodiscard]] key_compare key_comp() const {
    return key_compare{};
  }

  [[nodiscard]] size_type size() const noexcept {
    return nodes_.size();
  }

  [[nodiscard]] bool empty() const noexcept {
    return nodes_.empty();
  }

  void clear() noexcept {
    nodes_.clear();
    invert_.clear();
  }

  [[nodiscard]] iterator begin() noexcept {
    return iterator{nodes_.begin()};
  }

  [[nodiscard]] const_iterator begin() const noexcept {
    return const_iterator{nodes_.begin()};
  }

  [[nodiscard]] const_iterator cbegin() const noexcept {
    return begin();
  }

  [[nodiscard]] iterator end() noexcept {
    return iterator{nodes_.end()};
  }

  [[nodiscard]] const_iterator end() const noexcept {
    return const_iterator{nodes_.end()};
  }

  [[nodiscard]] const_iterator cend() const noexcept {
    return end();
  }

  std::pair<iterator, bool> insert(std::span<const Element_> key, Value_ value) {
    norm_helper norm(key);
    auto it = nodes_.find(norm.span());
    if (it != nodes_.end()) {
      return {iterator{it}, false};
    }
    auto [ins_it, inserted] = nodes_.try_emplace(norm.to_vector(), std::move(value));
    if (inserted) {
      index(*ins_it);
    }
    return {iterator{ins_it}, inserted};
  }

  std::pair<iterator, bool> insert(std::initializer_list<Element_> key, Value_ value) {
    return insert(std::span<const Element_>{key.begin(), key.end()}, std::move(value));
  }

  [[nodiscard]] iterator find(std::span<const Element_> key) {
    norm_helper norm(key);
    return iterator{nodes_.find(norm.span())};
  }

  [[nodiscard]] const_iterator find(std::span<const Element_> key) const {
    norm_helper norm(key);
    return const_iterator{nodes_.find(norm.span())};
  }

  [[nodiscard]] iterator find(std::initializer_list<Element_> key) {
    return find(std::span<const Element_>{key.begin(), key.end()});
  }

  [[nodiscard]] const_iterator find(std::initializer_list<Element_> key) const {
    return find(std::span<const Element_>{key.begin(), key.end()});
  }

  [[nodiscard]] std::vector<Value_*> supersets(std::span<const Element_> query) {
    return query_supersets(*this, query);
  }

  [[nodiscard]] std::vector<const Value_*> supersets(std::span<const Element_> query) const {
    return query_supersets(*this, query);
  }

  [[nodiscard]] std::vector<Value_*> supersets(std::initializer_list<Element_> query) {
    return supersets(std::span<const Element_>{query.begin(), query.end()});
  }

  [[nodiscard]] std::vector<const Value_*> supersets(std::initializer_list<Element_> query) const {
    return supersets(std::span<const Element_>{query.begin(), query.end()});
  }

  bool erase(std::span<const Element_> key) {
    norm_helper norm(key);
    auto it = nodes_.find(norm.span());
    if (it == nodes_.end()) {
      return false;
    }
    unindex(*it);
    nodes_.erase(it);
    return true;
  }

  bool erase(std::initializer_list<Element_> key) {
    return erase(std::span<const Element_>{key.begin(), key.end()});
  }

  iterator erase(const_iterator pos) {
    if (pos == cend()) {
      throw std::out_of_range("inverted_map::erase");
    }
    auto map_it = pos.base();
    unindex(*map_it);
    return iterator{nodes_.erase(map_it)};
  }

private:
  void reindex() {
    invert_.clear();
    for (entry& stored : nodes_) {
      index(stored);
    }
  }

  void index(entry& stored) {
    for (const Element_& element : stored.first) {
      invert_[element].push_back(&stored);
    }
  }

  void unindex(const entry& stored) {
    const entry* target = &stored;
    for (const Element_& element : stored.first) {
      auto it = invert_.find(element);
      if (it == invert_.end()) {
        continue;
      }
      auto& list = it->second;
      auto pos = std::find_if(list.begin(), list.end(), [target](const entry* item) {
        return item == target;
      });
      if (pos != list.end()) {
        *pos = list.back();
        list.pop_back();
      }
      if (list.empty()) {
        invert_.erase(it);
      }
    }
  }

  template <typename Self_>
  [[nodiscard]] static auto query_supersets(Self_&& self, std::span<const Element_> query) {
    using ValuePtr_ = std::conditional_t<
        std::is_const_v<std::remove_reference_t<Self_>>,
        const Value_*,
        Value_*>;
    std::vector<ValuePtr_> out;

    norm_helper norm(query);
    auto q_span = norm.span();

    if (q_span.empty()) {
      out.reserve(self.nodes_.size());
      for (auto&& stored : self.nodes_) {
        out.push_back(&stored.second);
      }
      return out;
    }

    // Find posting list with minimal length
    const std::vector<entry*>* smallest = nullptr;
    for (const Element_& element : q_span) {
      auto it = self.invert_.find(element);
      if (it == self.invert_.end()) {
        return out;
      }
      if (smallest == nullptr || it->second.size() < smallest->size()) {
        smallest = &it->second;
      }
    }

    out.reserve(smallest->size());
    for (auto* stored : *smallest) {
      if (std::includes(stored->first.begin(), stored->first.end(),
                        q_span.begin(), q_span.end(), ElementLess_{})) {
        out.push_back(&stored->second);
      }
    }
    return out;
  }
};

} // namespace byte
