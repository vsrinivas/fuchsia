// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_UNION_FIND_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_UNION_FIND_H_

#include <unordered_map>

namespace harvester {

// Implements a simple "union find"/"disjoint set" data structure. See
// https://en.wikipedia.org/wiki/Disjoint-set_data_structure for more
// information.
// NOTE: for simplicity, T is assumed to be a simple type that is trivially
// copyable. zx_koid_t (as a uint64_t) is a good example of this.
template <typename T>
class UnionFind {
 public:
  UnionFind() = default;

  // Adds |element| to the forest.
  //
  // Find() calls MakeSet() to ensure it never operates on an invalid element,
  // so it's not necessary to call MakeSet() on an element before acting on it.
  // However, calling MakeSet() better expresses intent.
  void MakeSet(T element) {
    if (parent_.count(element)) {
      return;
    }

    parent_.emplace(element, element);
  }

  // Find the representative element for |element|. Even when many items are in
  // a set, this is guaranteed to have a stable answer. This has amortized
  // ~constant time for all meaningful forest sizes.
  T Find(T element) {
    // Ensure element is in the forest.
    MakeSet(element);

    // Base case: this is the representative element of the set.
    if (parent_[element] == element) {
      return element;
    }

    // Recurse: compress the path. Trees are shallow, so this should never
    // overflow.
    return parent_[element] = Find(parent_[element]);
  }

  // Given two elements, merge their sets.
  //
  // NOTE: Do NOT rely on this being stable across builds; we may eventually
  // want a more efficient Find/Union operation that changes ordering here and
  // results in a different representative element per set.
  void Union(T a, T b) {
    // Get the representative element for a and b.
    T a_repr = Find(a);
    T b_repr = Find(b);

    // Do nothing if a and b are in the same set.
    if (a_repr == b_repr) {
      return;
    }

    // Ensure a_repr >= b_repr rank-wise.
    // NOTE: operator[] inserts a 0 value if it doesn't exist, which is fine.
    if (rank_[a_repr] < rank_[b_repr]) {
      std::swap(a_repr, b_repr);
    }

    // Have a_repr "adopt" the smaller set represented by b_repr.
    parent_[b_repr] = a_repr;
    if (rank_[a_repr] == rank_[b_repr]) {
      ++rank_[a_repr];
      // b_repr is no longer a representative element; its rank will never be
      // accessed again.
      rank_.erase(b_repr);
    }
  }

  // Returns true iff the given elements are in the same set. This isn't part of
  // the canonical definition of UnionFind, but it's a common operation built
  // from Find().
  bool InSameSet(T a, T b) {
    return Find(a) == Find(b);
  }

 private:
  // The maximum rank value for a given UnionFind is rigorously upper-bounded by
  // floor(log2(num of elements)). uint8_t allows up to 2^256-1 elements, which
  // is more than any type T will ever need.
  using Rank = uint8_t;

  // A map of each T value to a parent that is part of its set (aka a linked
  // list/tree). Root/singleton elements will point to themselves.
  std::unordered_map<T, T> parent_;
  // A map of elements to their respective ranks. Ranks are what the height of
  // each tree in the forest _would be_ if Find() did not do path compression.
  // An element's rank is only changed (incremented) when it becomes the parent
  // for another tree of equal rank. Only representative elements need ranks.
  std::unordered_map<T, Rank> rank_;
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_UNION_FIND_H_

