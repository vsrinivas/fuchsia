// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/internal/bindings_serialization.h"
#include "lib/fidl/cpp/bindings/internal/fixed_buffer.h"

namespace fidl {
namespace test {
namespace {

bool IsZero(void* p_buf, size_t size) {
  char* buf = reinterpret_cast<char*>(p_buf);
  for (size_t i = 0; i < size; ++i) {
    if (buf[i] != 0)
      return false;
  }
  return true;
}

// Tests that you can create a FixedBuffer whose underlying buffer size is not
// a multiple of 8.
TEST(FixedBufferTest, UnAlignedBufferSized) {
  char char_buf[10] = {};
  internal::FixedBuffer fixed_buf;
  fixed_buf.Initialize(char_buf, sizeof(char_buf));
}

// Tests that FixedBuffer allocates memory aligned to 8 byte boundaries.
TEST(FixedBufferTest, Alignment) {
  internal::FixedBufferForTesting buf(internal::Align(10) * 2);
  ASSERT_EQ(buf.size(), 16u * 2);

  void* a = buf.Allocate(10);
  ASSERT_TRUE(a);
  EXPECT_TRUE(IsZero(a, 10));
  EXPECT_EQ(0, reinterpret_cast<ptrdiff_t>(a) % 8);

  void* b = buf.Allocate(10);
  ASSERT_TRUE(b);
  EXPECT_TRUE(IsZero(b, 10));
  EXPECT_EQ(0, reinterpret_cast<ptrdiff_t>(b) % 8);

  // Any more allocations would result in an assert, but we can't test that.
}

// Tests that FixedBufferForTesting::Leak passes ownership to the caller.
TEST(FixedBufferTest, Leak) {
  void* ptr = nullptr;
  void* buf_ptr = nullptr;
  {
    internal::FixedBufferForTesting buf(8);
    ASSERT_EQ(8u, buf.size());

    ptr = buf.Allocate(8);
    ASSERT_TRUE(ptr);
    buf_ptr = buf.Leak();

    // The buffer should point to the first element allocated.
    // TODO(mpcomplete): Is this a reasonable expectation?
    EXPECT_EQ(ptr, buf_ptr);

    // The FixedBufferForTesting should be empty now.
    EXPECT_EQ(0u, buf.size());
    EXPECT_FALSE(buf.Leak());
  }

  // Since we called Leak, ptr is still writable after FixedBufferForTesting
  // went out of scope.
  memset(ptr, 1, 8);
  free(buf_ptr);
}

#if defined(NDEBUG) && !defined(DCHECK_ALWAYS_ON)
TEST(FixedBufferTest, TooBig) {
  internal::FixedBufferForTesting buf(24);

  // A little bit too large.
  EXPECT_EQ(reinterpret_cast<void*>(0), buf.Allocate(32));

  // Move the cursor forward.
  EXPECT_NE(reinterpret_cast<void*>(0), buf.Allocate(16));

  // A lot too large.
  EXPECT_EQ(reinterpret_cast<void*>(0),
            buf.Allocate(std::numeric_limits<size_t>::max() - 1024u));

  // A lot too large, leading to possible integer overflow.
  EXPECT_EQ(reinterpret_cast<void*>(0),
            buf.Allocate(std::numeric_limits<size_t>::max() - 8u));
}
#endif

}  // namespace
}  // namespace test
}  // namespace fidl
