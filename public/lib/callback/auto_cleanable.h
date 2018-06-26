// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CALLBACK_AUTO_CLEANABLE_H_
#define LIB_CALLBACK_AUTO_CLEANABLE_H_

#include <functional>
#include <map>
#include <unordered_set>
#include <utility>

#include <lib/fit/function.h>
#include "lib/fxl/logging.h"

namespace callback {

// List that will delete its elements when they call their on_empty_callback.
// The elements must have a setter method:
// |void set_on_empty(fit::closure on_empty)|.
template <typename V>
class AutoCleanableSet {
 public:
  class ReferenceEquality {
   public:
    bool operator()(const V& v1, const V& v2) const { return &v1 == &v2; }
  };

  class ReferenceHash {
   public:
    uintptr_t operator()(const V& v) const {
      return reinterpret_cast<uintptr_t>(&v);
    }
  };

  using Set_ = typename std::unordered_set<V, ReferenceHash, ReferenceEquality>;
  class iterator : public std::iterator<std::forward_iterator_tag, V> {
   public:
    explicit iterator(typename Set_::iterator base) : base_(base) {}

    iterator& operator++() {
      ++base_;
      return *this;
    }

    iterator operator++(int) {
      iterator result(base_);
      operator++();
      return result;
    }

    bool operator==(const iterator& rhs) const { return base_ == rhs.base_; }
    bool operator!=(const iterator& rhs) const { return base_ != rhs.base_; }
    // This set uses the reference of the object (and not its contents) to
    // define object equality. Thus, calling non-const methods on the stored
    // objects does not change their hash, and they are safe to use.
    V& operator*() const { return const_cast<V&>(*(base_)); }
    V* operator->() const { return const_cast<V*>(base_.operator->()); }

   private:
    typename Set_::iterator base_;
  };

  AutoCleanableSet() {}
  ~AutoCleanableSet() {}

  AutoCleanableSet(AutoCleanableSet&& other) noexcept = default;
  AutoCleanableSet& operator=(AutoCleanableSet&& other) noexcept = default;

  bool empty() { return set_.empty(); }

  void clear() { set_.clear(); }

  template <class... Args>
  V& emplace(Args&&... args) {
    auto pair = set_.emplace(std::forward<Args>(args)...);
    FXL_DCHECK(pair.second);
    // Set iterators are const because modifying the element would change the
    // hash. In this particular case, this is safe because this set uses
    // reference equality.
    V& item = const_cast<V&>(*(pair.first));
    item.set_on_empty([this, &item] {
      size_t erase_count = set_.erase(item);
      FXL_DCHECK(erase_count == 1);
      CheckEmpty();
    });
    return item;
  }

  iterator begin() { return iterator(set_.begin()); }

  iterator end() { return iterator(set_.end()); }

  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

 private:
  static bool Equals(const V& v1, const V& v2) { return v1 == v2; };

  static std::size_t Hash(const V& v1) { return &v1; };

  void CheckEmpty() {
    if (set_.empty() && on_empty_callback_)
      on_empty_callback_();
  }

  Set_ set_;
  fit::closure on_empty_callback_;
};

// Map that will delete its elements when they call their on_empty_callback.
// The elements must have a setter method:
// |void set_on_empty(fit::closure)|.
template <typename K, typename V, typename Compare = std::less<K>>
class AutoCleanableMap {
 public:
  using Map_ = typename std::map<K, V, Compare>;
  using iterator = typename Map_::iterator;
  using const_iterator = typename Map_::const_iterator;

  AutoCleanableMap() {}
  ~AutoCleanableMap() {}

  bool empty() { return map_.empty(); }

  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    auto result = map_.emplace(std::forward<Args>(args)...);
    if (result.second) {
      auto& key = result.first->first;
      auto& value = result.first->second;
      value.set_on_empty([this, key]() {
        map_.erase(key);
        CheckEmpty();
      });
    }
    return result;
  }

  void erase(iterator pos) {
    map_.erase(pos);
    CheckEmpty();
  }

  template <class KR>
  iterator find(const KR& x) {
    return map_.find(x);
  }

  template <class KR>
  const_iterator find(const KR& x) const {
    return map_.find(x);
  }

  iterator begin() { return map_.begin(); }

  const_iterator begin() const { return map_.begin(); }

  iterator end() { return map_.end(); }

  template <class KR>
  const_iterator end() const {
    return map_.end();
  }

  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

  size_t size() const { return map_.size(); }

 private:
  void CheckEmpty() {
    if (map_.empty() && on_empty_callback_)
      on_empty_callback_();
  }

  Map_ map_;
  fit::closure on_empty_callback_;
};

}  // namespace callback

#endif  // LIB_CALLBACK_AUTO_CLEANABLE_H_
