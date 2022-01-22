// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_INTEGRATION_ARENA_CHECKER_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_INTEGRATION_ARENA_CHECKER_H_

#include <lib/fidl/llcpp/arena.h>

namespace fidl_testing {

class ArenaChecker {
 public:
  template <size_t kInitialCapacity>
  static bool IsPointerInArena(void* pointer, ::fidl::Arena<kInitialCapacity>& arena) {
    return IsPointerInArena(pointer, arena, arena.initial_buffer_, kInitialCapacity);
  }

  // Returns if the arena was used to allocate any buffer at all.
  template <size_t kInitialCapacity>
  static bool DidUse(::fidl::Arena<kInitialCapacity>& arena) {
    return DidUse(arena, arena.initial_buffer_);
  }

 private:
  static bool IsPointerInArena(void* pointer, ::fidl::ArenaBase& arena,
                               const uint8_t* initial_buffer, size_t initial_capacity);

  static bool DidUse(::fidl::ArenaBase& arena, const uint8_t* initial_buffer);
};

}  // namespace fidl_testing

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_INTEGRATION_ARENA_CHECKER_H_
