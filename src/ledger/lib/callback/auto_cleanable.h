// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_CALLBACK_AUTO_CLEANABLE_H_
#define SRC_LEDGER_LIB_CALLBACK_AUTO_CLEANABLE_H_

#include <lib/async/default.h>
#include <lib/fit/function.h>

#include <functional>
#include <map>
#include <unordered_set>
#include <utility>

#include "src/ledger/lib/callback/scoped_task_runner.h"
#include "src/ledger/lib/logging/logging.h"

namespace ledger {

// List that will delete its elements when they call their on_discardable.
// The elements must have a setter method:
// |void SetOnDiscardable(fit::closure on_discardable)|
// and another to check whether they can be discarded:
// |bool IsDiscardable()|.
template <typename V>
class AutoCleanableSet {
 public:
  class ReferenceEquality {
   public:
    bool operator()(const V& v1, const V& v2) const { return &v1 == &v2; }
  };

  class ReferenceHash {
   public:
    uintptr_t operator()(const V& v) const { return reinterpret_cast<uintptr_t>(&v); }
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

  AutoCleanableSet(async_dispatcher_t* dispatcher) : task_runner_(dispatcher) {}
  AutoCleanableSet(const AutoCleanableSet<V>& other) = delete;
  ~AutoCleanableSet() {}

  AutoCleanableSet<V>& operator=(const AutoCleanableSet<V>& other) = delete;

  // Capacity methods.
  size_t size() const { return set_.size(); }
  bool empty() const { return set_.empty(); }

  void clear() {
    task_runner_.Reset();
    set_.clear();
  }

  template <class... Args>
  V& emplace(Args&&... args) {
    auto pair = set_.emplace(std::forward<Args>(args)...);
    LEDGER_DCHECK(pair.second);
    // Set iterators are const because modifying the element would change the
    // hash. In this particular case, this is safe because this set uses
    // reference equality.
    V& item = const_cast<V&>(*(pair.first));
    item.SetOnDiscardable([this, &item] {
      task_runner_.PostTask([this, &item] {
        auto it = set_.find(item);
        if (it == set_.end() || !it->IsDiscardable()) {
          return;
        }
        set_.erase(it);
        CheckDiscardable();
      });
    });
    return item;
  }

  iterator begin() { return iterator(set_.begin()); }

  iterator end() { return iterator(set_.end()); }

  void SetOnDiscardable(fit::closure on_discardable) {
    on_discardable_ = std::move(on_discardable);
  }

  bool IsDiscardable() const { return empty(); }

 private:
  static bool Equals(const V& v1, const V& v2) { return v1 == v2; };

  static std::size_t Hash(const V& v1) { return &v1; };

  void CheckDiscardable() {
    if (IsDiscardable() && on_discardable_)
      on_discardable_();
  }

  Set_ set_;
  fit::closure on_discardable_;

  // Must be the last member of the class.
  ScopedTaskRunner task_runner_;
};

// Map that will delete its elements when they call their on_discardable.
// The elements must have a setter method:
// |void SetOnDiscardable(fit::closure on_discardable)|
// and another to check whether they can be discarded:
// |bool IsDiscardable()|.
template <typename K, typename V, typename Compare = std::less<K>>
class AutoCleanableMap {
 public:
  using Map_ = typename std::map<K, V, Compare>;
  using iterator = typename Map_::iterator;
  using const_iterator = typename Map_::const_iterator;

  AutoCleanableMap(async_dispatcher_t* dispatcher) : task_runner_(dispatcher) {}
  AutoCleanableMap(const AutoCleanableMap<K, V, Compare>& other) = delete;
  ~AutoCleanableMap() {}

  AutoCleanableMap<K, V, Compare>& operator=(const AutoCleanableMap<K, V, Compare>& other) = delete;

  template <typename Key, typename... Args>
  std::pair<iterator, bool> try_emplace(Key&& key, Args&&... args) {
    auto result = map_.try_emplace(std::forward<Key>(key), std::forward<Args>(args)...);
    if (result.second) {
      auto& key = result.first->first;
      auto& value = result.first->second;
      value.SetOnDiscardable([this, key]() {
        task_runner_.PostTask([this, key] {
          auto it = map_.find(key);
          if (it != map_.end() && it->second.IsDiscardable()) {
            map_.erase(it);
          }
          CheckDiscardable();
        });
      });
    }
    return result;
  }

  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    auto result = map_.emplace(std::forward<Args>(args)...);
    if (result.second) {
      auto& key = result.first->first;
      auto& value = result.first->second;
      value.SetOnDiscardable([this, key] {
        task_runner_.PostTask([this, key] {
          auto it = map_.find(key);
          if (it == map_.end() || !it->second.IsDiscardable()) {
            return;
          }
          map_.erase(it);
          CheckDiscardable();
        });
      });
    }
    return result;
  }

  void erase(iterator pos) {
    map_.erase(pos);
    CheckDiscardable();
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

  const_iterator end() const { return map_.end(); }

  void SetOnDiscardable(fit::closure on_discardable) {
    on_discardable_ = std::move(on_discardable);
  }

  bool IsDiscardable() const { return empty(); }

  size_t size() const { return map_.size(); }

  bool empty() const { return map_.empty(); }

  void clear() {
    task_runner_.Reset();
    map_.clear();
  }

 private:
  void CheckDiscardable() {
    if (IsDiscardable() && on_discardable_) {
      on_discardable_();
    }
  }

  Map_ map_;
  fit::closure on_discardable_;

  // Must be the last member of the class.
  ScopedTaskRunner task_runner_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_CALLBACK_AUTO_CLEANABLE_H_
