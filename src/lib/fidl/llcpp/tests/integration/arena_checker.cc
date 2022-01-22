// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arena_checker.h"

namespace fidl_testing {

bool ArenaChecker::IsPointerInArena(void* pointer, ::fidl::ArenaBase& arena,
                                    const uint8_t* initial_buffer, size_t initial_capacity) {
  uint8_t* data = static_cast<uint8_t*>(pointer);
  auto in_range = [=](const uint8_t* start, size_t len) -> bool {
    if (data > start) {
      if (data - start < static_cast<ptrdiff_t>(len)) {
        return true;
      }
    }
    return false;
  };

  // Check presence in initial buffer.
  if (in_range(initial_buffer, initial_capacity)) {
    return true;
  }
  // Check presence in each extra block.
  fidl::ArenaBase::ExtraBlock* last_extra_block = arena.last_extra_block_;
  while (last_extra_block != nullptr) {
    auto* to_check = last_extra_block;
    last_extra_block = last_extra_block->next_block();
    if (in_range(to_check->data(), to_check->size())) {
      return true;
    }
  }
  return false;
}

bool ArenaChecker::DidUse(::fidl::ArenaBase& arena, const uint8_t* initial_buffer) {
  // If we are not at the start of the initial buffer, then allocation had happened.
  return arena.next_data_available_ != initial_buffer;
}

}  // namespace fidl_testing
