// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/function.h>

// fbl::Function is thoroughly tested in userspace tests. The kernel uses a
// restricted form of the interface, by only allowing inline (rather than heap)
// allocated Functions.
//
// Thus, this file only contains some negative compilation tests.

[[maybe_unused]] static void wont_compile_heap_functions() {
  constexpr size_t kTooBig = fbl::kDefaultInlineCallableSize * 2;
  char too_big[kTooBig];
  // Capture by value, not reference.
  __UNUSED auto lambda = [too_big]() { return too_big[0]; };
#if TEST_WILL_NOT_COMPILE || 0
  fbl::Function<char()> f{lambda};
#endif
}
