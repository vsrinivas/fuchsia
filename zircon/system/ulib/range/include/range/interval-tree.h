// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RANGE_INTERVAL_TREE_H_
#define RANGE_INTERVAL_TREE_H_

#include <zircon/assert.h>

#include <map>
#include <utility>

#include <range/range.h>

namespace range {

// An associative container which holds ranges of values.
// IntervalTree is capable of holding these ranges of values, but indexing by individual
// values, instead of by range.
//
// This class is thread-compatible.
template <typename Range>
class IntervalTree {
 public:
  using RangeType = Range;
  using KeyType = typename Range::KeyType;
  using MapType = std::map<KeyType, RangeType>;
  using IterType = typename MapType::iterator;
  using ConstIterType = typename MapType::const_iterator;

  IntervalTree() = default;

  // Inserts a range of values into the tree. If they overlap with existing ranges, they
  // are combined with those existing ranges. If this range cannot be combined with
  // existing ranges, an error is returned.
  //
  // Returns true if the range is inserted successfully.
  // Returns false if the insertion was unsuccessful (This is only possible with an error
  // propagated from |Range::Container::Update|).
  //
  // Runtime: O(log(number of ranges))
  template <typename RangeType>
  bool try_insert(RangeType&& range) {
    // Special case: Set is empty, range is inserted as-is.
    if (map_.empty()) {
      map_.insert({range.Start(), std::forward<RangeType>(range)});
      return true;
    }
    auto next = map_.upper_bound(range.Start());
    auto prior = next;

    // Merge with the subsequent elements, if possible.
    while (next != map_.end()) {
      zx_status_t status = range.Merge(next->second);
      if (status != ZX_OK && range::Overlap(next->second, range)) {
        // The ranges needed to merge (due to overlap), but could not.
        return status == ZX_OK;
      }
      if (status == ZX_OK) {
        // The ranges merged.
        ++next;
      } else {
        // The ranges did not (and don't need to) merge.
        break;
      }
    }

    // Merge with the prior elements, if possible.
    while (prior != map_.begin()) {
      prior--;
      zx_status_t status = range.Merge(prior->second);
      if (status != ZX_OK && range::Overlap(prior->second, range)) {
        // The ranges needed to merge (due to overlap), but could not.
        return status == ZX_OK;
      }
      if (status == ZX_OK) {
        // The ranges merged.
        continue;
      }
      // The ranges did not (and don't need to) merge.
      // Make sure prior only points to things we do need to erase.
      prior++;
      break;
    }

    if (prior != next) {
      map_.erase(prior, next);
    }
    map_.insert({range.Start(), std::forward<RangeType>(range)});
    return true;
  }

  // Inserts |range| of values into the tree.
  //
  // Precondition: |range| must be either mergeable with overlapping intervals in the tree, or must
  // not overlap. Callers which cannot satisfy these preconditions should consider |try_insert|
  // instead.
  template <typename RangeType>
  void insert(RangeType&& range) {
    ZX_ASSERT(try_insert(std::forward<RangeType>(range)));
  }

  // Erases a single value from the tree. If this value is only part of a range, that
  // range is split into multiple parts.
  //
  // Runtime: O(log(number of ranges))
  void erase(const KeyType& value) {
    auto iter = find(value);
    if (iter == map_.end()) {
      return;
    }

    // Remove the entire range containing the value.
    RangeType range = iter->second;
    map_.erase(iter);

    // If we cut a range in pieces, put the remaining valid pieces back.
    typename RangeType::Container prior_container = range.container();
    ZX_ASSERT(RangeType::ContainerTraits::Update(nullptr, range.Start(), value, &prior_container) ==
              ZX_OK);
    RangeType prior(prior_container);
    ZX_DEBUG_ASSERT(range.Start() == prior.Start());
    ZX_DEBUG_ASSERT(value == prior.End());
    if (prior.Length()) {
      map_.insert({prior.Start(), prior});
    }

    typename RangeType::Container next_container = range.container();
    ZX_ASSERT(RangeType::ContainerTraits::Update(nullptr, value + 1, range.End(),
                                                 &next_container) == ZX_OK);
    RangeType next(next_container);
    ZX_DEBUG_ASSERT(value + 1 == next.Start());
    ZX_DEBUG_ASSERT(range.End() == next.End());
    if (next.Length()) {
      map_.insert({next.Start(), next});
    }
  }

  // Erases a range from the tree. If this range partially overlaps with ranges present in the
  // tree, those ranges are split into multiple parts.
  void erase(const RangeType& value) {
    IterType iter;
    while ((iter = find(value)) != map_.end()) {
      RangeType range = iter->second;

      // Remove the entire overlapping in-tree range.
      map_.erase(iter);

      // If we cut a range in pieces, put the remaining valid pieces back.
      if (range.Start() < value.Start()) {
        typename RangeType::Container prior_container = range.container();
        ZX_ASSERT(RangeType::ContainerTraits::Update(nullptr, range.Start(), value.Start(),
                                                     &prior_container) == ZX_OK);
        RangeType prior(prior_container);
        map_.insert({prior.Start(), prior});
      }

      if (value.End() < range.End()) {
        typename RangeType::Container next_container = range.container();
        ZX_ASSERT(RangeType::ContainerTraits::Update(nullptr, value.End(), range.End(),
                                                     &next_container) == ZX_OK);
        RangeType next(next_container);
        map_.insert({next.Start(), next});
      }
    }
  }

  // Returns an iterator to the range which contains the value.
  // If no such range exists, returns |end()|.
  //
  // Runtime: O(log(number of ranges))
  IterType find(const KeyType& value) {
    if (map_.empty()) {
      return map_.end();
    }
    auto iter = map_.upper_bound(value);
    if (iter == map_.begin()) {
      // |value| is less than the first valid element.
      return map_.end();
    }
    iter--;
    if (iter == map_.end()) {
      return map_.end();
    }

    if (iter->second.Start() <= value && value < iter->second.End()) {
      return iter;
    }
    return map_.end();
  }

  // Returns an iterator to the first range which overlaps with a provided range.
  // If no such range exists, returns |end()|.
  IterType find(const RangeType& range) {
    if (map_.empty()) {
      return map_.end();
    }

    // Return the first element strictly after the start of the range.
    //
    // As long as this isn't the first element of |map_|, move back to the prior element.
    // This is necessary for using a single-element indexing scheme into a range-based
    // structure.
    auto iter = map_.upper_bound(range.Start());
    if (iter != map_.begin()) {
      iter--;
    }
    while (iter != map_.end() && iter->second.Start() < range.End()) {
      if (Overlap(iter->second, range)) {
        return iter;
      }
      iter++;
    }
    return map_.end();
  }

  void clear() { map_.clear(); }

  [[nodiscard]] bool empty() const { return map_.empty(); }

  [[nodiscard]] size_t size() const { return map_.size(); }

  IterType begin() { return map_.begin(); }

  ConstIterType begin() const { return map_.begin(); }

  IterType end() { return map_.end(); }

  ConstIterType end() const { return map_.end(); }

 private:
  MapType map_;
};

}  // namespace range

#endif  // RANGE_INTERVAL_TREE_H_
