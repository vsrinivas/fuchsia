// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_ID_MAP_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_ID_MAP_H_

#include <fbl/intrusive_hash_table.h>

namespace display {

// Helper for allowing structs which are identified by unique ids to be put in a hashmap.
template <typename T>
class IdMappable : public fbl::SinglyLinkedListable<T> {
 public:
  uint64_t id;

  static size_t GetHash(uint64_t id) { return id; }

  uint64_t GetKey() const { return id; }

  using Map = fbl::HashTable<uint64_t, T>;
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_ID_MAP_H_
