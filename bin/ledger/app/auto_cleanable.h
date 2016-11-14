// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_AUTO_CLEANABLE_H_
#define APPS_LEDGER_SRC_APP_AUTO_CLEANABLE_H_

#include <functional>
#include <map>
#include <unordered_set>
#include <utility>

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/logging.h"

namespace ledger {

// List that will delete its elements when they call their on_empty_callback.
// The elements must have a setter method:
// |void set_on_empty(const // ftl::Closure&)|.

template <typename V>
class AutoCleanableSet {
 public:
  class ReferenceEquality {
   public:
    bool operator()(const V& v1, const V& v2) { return &v1 == &v2; }
  };

  class ReferenceHash {
   public:
    uintptr_t operator()(const V& v) const {
      return reinterpret_cast<uintptr_t>(&v);
    }
  };

  using Set_ = typename std::unordered_set<V, ReferenceHash, ReferenceEquality>;
  using iterator = typename Set_::iterator;

  AutoCleanableSet() {}
  ~AutoCleanableSet() {}

  bool empty() { return set_.empty(); }

  template <class... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    auto pair = set_.emplace(std::forward<Args>(args)...);
    FTL_DCHECK(pair.second);
    // Set iterators are const because modifying the element would change the
    // hash. In this particular case, this is safe because this set uses
    // reference equality.
    V& item = const_cast<V&>(*(pair.first));
    item.set_on_empty([this, &item] {
      size_t erase_count = set_.erase(item);
      FTL_DCHECK(erase_count == 1);
      CheckEmpty();
    });
    return pair;
  }

  void set_on_empty(const ftl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

 private:
  static bool Equals(const V& v1, const V& v2) { return v1 == v2; };

  static std::size_t Hash(const V& v1) { return &v1; };

  void CheckEmpty() {
    if (set_.empty() && on_empty_callback_)
      on_empty_callback_();
  }

  Set_ set_;
  ftl::Closure on_empty_callback_;
};

// Map that will delete its elements when they call their on_empty_callback.
// The elements must have a setter method:
// |void set_on_empty(const // ftl::Closure&)|.
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

  iterator end() { return map_.end(); }

  template <class KR>
  const_iterator end() const {
    return map_.end();
  }

  void set_on_empty(const ftl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

 private:
  void CheckEmpty() {
    if (map_.empty() && on_empty_callback_)
      on_empty_callback_();
  }

  Map_ map_;
  ftl::Closure on_empty_callback_;
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_AUTO_CLEANABLE_H_
