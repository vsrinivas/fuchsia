// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cxxabi.h>
#include <zircon/assert.h>

#include <zxtest/zxtest.h>

// This is not declared anywhere.  The compiler passes its address in its
// implicit __cxa_atexit calls.
extern "C" void* __dso_handle;

namespace {

// The libc implementation supports some number before it does any
// dynamic allocation.  Make sure to test more than that many.
// Currently that's 32, but the implementation might change.
constexpr int kManyAtexit = 100;

int kData;

// This doesn't actually test very much inside the test itself.  The
// registered function validates that it was invoked correctly, so the
// assertion failure would make the executable fail after the test itself
// has succeeded.  But the real purpose of this test is just for the
// LeakSanitizer build to verify that `__cxa_atexit` itself doesn't leak
// internally.
TEST(AtExit, LeakCheck) {
  for (int i = 0; i < kManyAtexit; ++i) {
    EXPECT_EQ(
        0, abi::__cxa_atexit([](void* ptr) { ZX_ASSERT(ptr == &kData); }, &kData, &__dso_handle));
  }
}

}  // namespace
