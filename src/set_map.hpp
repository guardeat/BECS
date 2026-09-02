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
#include <unordered_set>
#include <utility>
#include <vector>

namespace byte {

/// @brief Map from sets to values, plus inverted-index superset queries.
template <typename Element_, typename Value_>
class set_map {
public:
  using element_type = Element_;
  using value_type = Value_;
  using size_type = std::size_t;
  using key_type = std::unordered_set<Element_>;

private:
  struct key_hash {
    [[nodiscard]] std::size_t operator()(const key_type& key) const {
      std::size_t hash = 0;
      for (const Element_& element : key) {
        hash ^= std::hash<Element_>{}(element);
      }
      return hash ^ key.size();
    }
  };

  using map_type = std::unordered_map<key_type, Value_, key_hash>;
  using entry = typename map_type::value_type;

  map_type nodes_{};
  std::unordered_map<Element_, std::vector<const entry*>> invert_{};

public:
  template <bool Const_>
  class basic_iterator {
  public:
    using iterator_concept = std::forward_iterator_tag;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Value_;
    using reference = std::conditional_t<Const_, const Value_&, Value_&>;
    using pointer = std::conditional_t<Const_, const Value_*, Value_*>;

  private:
    using MapIt_ = std::conditional_t<Const_, typename map_type::const_iterator, typename map_type::iterator>;
    MapIt_ it_{};

  public:
    basic_iterator() = default;
    explicit basic_iterator(MapIt_ it) : it_{it} {}

    [[nodiscard]] operator basic_iterator<true>() const
      requires (!Const_)
    {
      return basic_iterator<true>{it_};
    }

    [[nodiscard]] reference operator*() const {
      return it_->second;
    }

    [[nodiscard]] pointer operator->() const {
      return &it_->second;
    }

    basic_iterator& operator++() {
      ++it_;
      return *this;
    }

    basic_iterator operator++(int) {
      basic_iterator tmp = *this;
      ++*this;
      return tmp;
    }

    [[nodiscard]] bool operator==(const basic_iterator&) const = default;

    [[nodiscard]] MapIt_ map_position() const noexcept {
      return it_;
    }
  };

  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;

  set_map() = default;

  set_map(const set_map& other)
    requires std::copy_constructible<Value_>
      : nodes_(other.nodes_) {
    reindex();
  }

  set_map& operator=(const set_map& other)
    requires std::copy_constructible<Value_>
  {
    if (this != &other) {
      set_map tmp(other);
      *this = std::move(tmp);
    }
    return *this;
  }

  set_map(set_map&& other) noexcept
      : nodes_(std::move(other.nodes_)), invert_(std::move(other.invert_)) {}

  set_map& operator=(set_map&& other) noexcept {
    if (this != &other) {
      nodes_ = std::move(other.nodes_);
      invert_ = std::move(other.invert_);
    }
    return *this;
  }

  [[nodiscard]] size_type size() const noexcept {
    return nodes_.size();
  }

  [[nodiscard]] bool empty() const noexcept {
    return nodes_.empty();
  }

  void clear() {
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
    auto [it, inserted] = nodes_.try_emplace(make_key(key), std::move(value));
    if (inserted) {
      index(*it);
    }
    return {iterator{it}, inserted};
  }

  std::pair<iterator, bool> insert(std::initializer_list<Element_> key, Value_ value) {
    return insert(std::span<const Element_>{key.begin(), key.end()}, std::move(value));
  }

  [[nodiscard]] iterator find(std::span<const Element_> key) {
    return iterator{nodes_.find(make_key(key))};
  }

  [[nodiscard]] const_iterator find(std::span<const Element_> key) const {
    return const_iterator{nodes_.find(make_key(key))};
  }

  [[nodiscard]] iterator find(std::initializer_list<Element_> key) {
    return find(std::span<const Element_>{key.begin(), key.end()});
  }

  [[nodiscard]] const_iterator find(std::initializer_list<Element_> key) const {
    return find(std::span<const Element_>{key.begin(), key.end()});
  }

  [[nodiscard]] std::vector<Value_*> supersets(std::span<const Element_> query) {
    return collect_supersets<false>(query);
  }

  [[nodiscard]] std::vector<const Value_*> supersets(std::span<const Element_> query) const {
    return collect_supersets<true>(query);
  }

  [[nodiscard]] std::vector<Value_*> supersets(std::initializer_list<Element_> query) {
    return supersets(std::span<const Element_>{query.begin(), query.end()});
  }

  [[nodiscard]] std::vector<const Value_*> supersets(std::initializer_list<Element_> query) const {
    return supersets(std::span<const Element_>{query.begin(), query.end()});
  }

  bool erase(std::span<const Element_> key) {
    auto it = nodes_.find(make_key(key));
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
      throw std::out_of_range("set_map::erase");
    }
    auto map_it = pos.map_position();
    unindex(*map_it);
    auto next = nodes_.erase(map_it);
    return iterator{next};
  }

private:
  [[nodiscard]] static key_type make_key(std::span<const Element_> elements) {
    return key_type(elements.begin(), elements.end());
  }

  void reindex() {
    invert_.clear();
    for (entry& stored : nodes_) {
      index(stored);
    }
  }

  void index(const entry& stored) {
    for (const Element_& element : stored.first) {
      invert_[element].push_back(&stored);
    }
  }

  void unindex(const entry& stored) {
    const entry* p = &stored;
    for (const Element_& element : stored.first) {
      auto it = invert_.find(element);
      if (it == invert_.end()) {
        continue;
      }
      auto& list = it->second;
      list.erase(std::remove(list.begin(), list.end(), p), list.end());
      if (list.empty()) {
        invert_.erase(it);
      }
    }
  }

  template <bool Const_>
  [[nodiscard]] auto collect_supersets(std::span<const Element_> query) const {
    using ValuePtr_ = std::conditional_t<Const_, const Value_*, Value_*>;
    std::vector<ValuePtr_> out;

    const key_type want = make_key(query);
    if (want.empty()) {
      out.reserve(nodes_.size());
      for (const entry& stored : nodes_) {
        out.push_back(const_cast<ValuePtr_>(&stored.second));
      }
      return out;
    }

    const std::vector<const entry*>* start = nullptr;
    for (const Element_& element : want) {
      auto it = invert_.find(element);
      if (it == invert_.end()) {
        return out;
      }
      if (start == nullptr || it->second.size() < start->size()) {
        start = &it->second;
      }
    }

    out.reserve(start->size());
    for (const entry* stored : *start) {
      bool ok = true;
      for (const Element_& element : want) {
        if (!stored->first.contains(element)) {
          ok = false;
          break;
        }
      }
      if (ok) {
        out.push_back(const_cast<ValuePtr_>(&stored->second));
      }
    }
    return out;
  }
};

} // namespace byte