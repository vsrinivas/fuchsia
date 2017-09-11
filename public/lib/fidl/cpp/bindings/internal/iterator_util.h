// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_ITERATOR_UTIL_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_ITERATOR_UTIL_H_

#include <algorithm>

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/map.h"
#include "lib/fxl/logging.h"

namespace fidl {
namespace internal {

// |MapKeyIterator| and |MapValueIterator| are templated interfaces for
// iterating over a Map<>'s keys and its values, respectively.  They provide a
// begin() and end() for generating an STL-iterator.  The generated Iterator
// follows the BidirectionalIterator concept
// (http://en.cppreference.com/w/cpp/concept/BidirectionalIterator).
//
// Example usage:
//
//  Map<int,int> my_map;
//  my_map[1] = 2;
//  my_map[3] = 4;
//  my_map[5] = 6;
//  for (int key : MapKeyIterator<int,int>(&my_map)) {
//    std::cout << key << std::endl;
//  }
//  for (int val : MapValueIterator<int,int>(&my_map)) {
//    std::cout << val << std::endl;
//  }

// Interface for iterating over a Map<K, V>'s keys.
// To construct a |MapKeyIterator|, pass in a non-null pointer to a Map<K, V>;
template <typename K, typename V>
class MapKeyIterator {
 public:
  class Iterator {
   public:
    Iterator() : it_() {}
    explicit Iterator(typename Map<K, V>::MapIterator it) : it_(it) {}
    Iterator& operator++() {
      ++it_;
      return *this;
    }
    Iterator operator++(int) {
      Iterator original = *this;
      ++it_;
      return original;
    }
    Iterator& operator--() {
      --it_;
      return *this;
    }
    Iterator operator--(int) {
      Iterator original = *this;
      --it_;
      return original;
    }
    bool operator==(const Iterator& o) const { return o.it_ == it_; }
    bool operator!=(const Iterator& o) const { return o.it_ != it_; }
    const K& operator*() { return it_.GetKey(); }
    const K* operator->() { return &it_.GetKey(); }

   private:
    typename Map<K, V>::MapIterator it_;
  };

  explicit MapKeyIterator(Map<K, V>* map) : map_(map) { FXL_DCHECK(map); }

  size_t size() const { return map_->size(); }
  Iterator begin() const { return Iterator{map_->begin()}; }
  Iterator end() const { return Iterator{map_->end()}; }

 private:
  Map<K, V>* const map_;
};

// Interface for iterating over a Map<K, V>'s values.
template <typename K, typename V>
class MapValueIterator {
 public:
  class Iterator {
   public:
    Iterator() : it_(typename Map<K, V>::MapIterator()) {}
    explicit Iterator(typename Map<K, V>::MapIterator it) : it_(it) {}
    Iterator& operator++() {
      ++it_;
      return *this;
    }
    Iterator operator++(int) {
      Iterator original = *this;
      ++it_;
      return original;
    }
    Iterator& operator--() {
      --it_;
      return *this;
    }
    Iterator operator--(int) {
      Iterator original = *this;
      --it_;
      return original;
    }
    bool operator==(const Iterator& o) const { return o.it_ == it_; }
    bool operator!=(const Iterator& o) const { return o.it_ != it_; }
    V& operator*() { return it_.GetValue(); }
    V* operator->() { return &it_.GetValue(); }

   private:
    typename Map<K, V>::MapIterator it_;
  };

  explicit MapValueIterator(Map<K, V>* map) : map_(map) { FXL_DCHECK(map); }
  size_t size() const { return map_->size(); }
  Iterator begin() const { return Iterator{map_->begin()}; }
  Iterator end() const { return Iterator{map_->end()}; }

 private:
  Map<K, V>* const map_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_ITERATOR_UTIL_H_
