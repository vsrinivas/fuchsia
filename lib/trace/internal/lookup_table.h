// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_INTERNAL_LOOKUP_TABLE_H_
#define APPS_TRACING_LIB_TRACE_INTERNAL_LOOKUP_TABLE_H_

#include "stdint.h"

#include <atomic>

#include "lib/ftl/macros.h"

namespace tracing {
namespace internal {

// A specialized lock-free hash table which assigns each key a unique index.
// The index can then be used to lookup an associated
// |std::atomic<uint32_t>| value.
//
// - Keys must be plain old data.
// - Values are initialized to zero when keys are inserted.
// - Indices range between 1 and |max_values| inclusively, with 0 reserved to
//   indicate find or insertion failure.
// - Hash is a functor which yields a value between 0 and (|max_chains| - 1)
//   for each key.
// - |max_chains| is the maximum number of hash collision chains.
// - |max_values| is the maximum number of values which can be stored
//   (also the maximum index).
template <typename Key, typename Hash, uint32_t max_chains, uint32_t max_values>
class LookupTable final {
 public:
  LookupTable() {
    memset(chains_, 0, sizeof(chains_));
    memset(slots_, 0, sizeof(slots_));
  }
  ~LookupTable() {}

  // Gets a reference to the value at the specified index.
  std::atomic<uint32_t>& operator[](uint32_t index) {
    return slots_[index - 1u].value;
  }

  // Inserts a key if not already present and returns its index.
  // Returns 0 if the table is full.
  uint32_t Insert(const Key& key) {
    const uint32_t chain = Hash()(key);
    uint32_t new_index = 0u;
    std::atomic<uint32_t>* link = &chains_[chain];
    for (;;) {
      uint32_t index = link->load(std::memory_order_acquire);
      if (index) {
        Slot& slot = slots_[index - 1u];
        if (key == slot.key)
          return index;
        link = &slot.next_index;
        continue;
      }

      // Try to add the item, loop if another insertion snuck in first.
      // This implementation has a deficiency wherein we may allocate a
      // slot and then not use it if the same key is inserted concurrently.
      // However, this is mostly harmless for our use-case and far simpler
      // than the alternatives.
      if (!new_index) {
        new_index = next_index_.fetch_add(1u, std::memory_order_relaxed);
        if (new_index > max_values) {
          // Prevent wrapping.
          next_index_.store(max_values + 1u, std::memory_order_relaxed);
          return 0u;
        }
        Slot& slot = slots_[new_index - 1u];
        slot.key = key;
      }
      if (link->compare_exchange_weak(index, new_index,
                                      std::memory_order_acquire,
                                      std::memory_order_release))
        return new_index;
    }
  }

 private:
  // Collision chains, indexed by hash code.
  std::atomic<uint32_t> chains_[max_chains];

  // Storage slots, indexed by key index.
  struct Slot {
    Key key;
    std::atomic<uint32_t> value;
    std::atomic<uint32_t> next_index;
  };
  Slot slots_[max_values];

  // Next usable slot index.
  std::atomic<uint32_t> next_index_{1u};

  FTL_DISALLOW_COPY_AND_ASSIGN(LookupTable);
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_LOOKUP_TABLE_H_
