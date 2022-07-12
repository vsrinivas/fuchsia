// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/stdcompat/source_location.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>

#include <gtest/gtest.h>

#include "user-copy.h"

// The goal of this test is to verify the behavior of each unrolled loop of the usercopy impl,
// with and without alignment. Later we can swap the user_copy variant, and verify it behaves as
// expected.

void DoAndVerifyCopy(size_t copy_size, size_t src_offset, size_t dst_offset,
                     cpp20::source_location loc = cpp20::source_location::current()) {
  SCOPED_TRACE("Invocation At: " + std::string(loc.file_name()) + ":" + std::to_string(loc.line()));
  SCOPED_TRACE("Args: copy_size: " + std::to_string(copy_size) + " src_offset: " +
               std::to_string(src_offset) + " dst_offset: " + std::to_string(dst_offset));
  constexpr std::array<uint8_t, 16> kCanary = {
      0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF,
  };
  constexpr uint8_t kAlignmentFill = 0xAA;

  auto get_size = [&](size_t offset) { return offset + copy_size + 2 * kCanary.size(); };

  auto del = [](auto* ptr) { ::operator delete[](ptr, std::align_val_t{16}); };
  using deleter = decltype(del);

  auto src_buffer = std::unique_ptr<uint8_t[], deleter>(
      new (std::align_val_t{16}) uint8_t[get_size(src_offset)], del);
  auto dst_buffer = std::unique_ptr<uint8_t[], deleter>(
      new (std::align_val_t{16}) uint8_t[get_size(dst_offset)], del);

  auto src = cpp20::span(src_buffer.get(), get_size(src_offset));
  auto dst = cpp20::span(dst_buffer.get(), get_size(dst_offset));
  // Randomize the contents of src and destination.
  unsigned int seed = ::testing::UnitTest::GetInstance()->random_seed();
  auto fill_random = [&](auto& buffer, size_t copy_size, size_t offset) {
    for (size_t i = 0; i < buffer.size(); ++i) {
      // Front canary.
      if (i < kCanary.size()) {
        buffer[i] = kCanary[i];
      } else if (i < kCanary.size() + offset) {  // Padding bytes with a fixed pattern.
        buffer[i] = kAlignmentFill;
      } else if (i < kCanary.size() + offset + copy_size) {  // Actual region to copy.
        buffer[i] = static_cast<uint8_t>(rand_r(&seed) % std::numeric_limits<uint8_t>::max());
      } else {  // Back Canary.
        buffer[i] = kCanary[i - (kCanary.size() + copy_size + offset)];
      }
    }
  };

  fill_random(src, copy_size, src_offset);
  fill_random(dst, copy_size, dst_offset);

  uint64_t fault_return = 0;
  auto [status, fault_flags, fault_addr] =
      ARM64_USERCOPY_FN(dst.data() + kCanary.size() + dst_offset,
                        src.data() + kCanary.size() + src_offset, copy_size, &fault_return, 0);

  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(fault_return, 0u);

  // Verify contents.
  for (size_t i = 0; i < dst.size(); i++) {
    if (i < kCanary.size()) {
      ASSERT_EQ(dst[i], kCanary[i]);
    } else if (i < kCanary.size() + dst_offset) {
      ASSERT_EQ(dst[i], kAlignmentFill);
    } else if (i < kCanary.size() + copy_size + dst_offset) {
      size_t region_offset = i - (kCanary.size() + dst_offset);
      ASSERT_EQ(dst[i], src[kCanary.size() + src_offset + region_offset]);
    } else {
      ASSERT_EQ(dst[i], kCanary[i - (kCanary.size() + copy_size + dst_offset)]);
    }
  }
}

// Each test case represents an internal unrolled loop branch. The goal is to verify individual
// branches for correctness.

TEST(Arm64UsercopyTest, 16Bytes) {
  constexpr size_t kMaxCopySize = 16;
  constexpr size_t kAlignBoundary = 16;

  for (size_t copy_size = 0; copy_size < kMaxCopySize; ++copy_size) {
    for (size_t src_offset = 0; src_offset < kAlignBoundary; ++src_offset) {
      for (size_t dst_offset = 0; dst_offset < kAlignBoundary; ++dst_offset) {
        DoAndVerifyCopy(copy_size, src_offset, dst_offset);
      }
    }
  }
}

TEST(Arm64UsercopyTest, 17to96Bytes) {
  constexpr size_t kMaxCopySize = 96;
  constexpr size_t kMinCopySize = 17;
  constexpr size_t kAlignBoundary = 16;

  for (size_t copy_size = kMinCopySize; copy_size < kMaxCopySize; ++copy_size) {
    for (size_t src_offset = 0; src_offset < kAlignBoundary; ++src_offset) {
      for (size_t dst_offset = 0; dst_offset < kAlignBoundary; ++dst_offset) {
        DoAndVerifyCopy(copy_size, src_offset, dst_offset);
      }
    }
  }
}

TEST(Arm64UsercopyTest, LongCopy) {
  constexpr size_t kMaxCopySize = 257;
  constexpr size_t kMinCopySize = 97;
  constexpr size_t kAlignBoundary = 16;

  for (size_t copy_size = kMinCopySize; copy_size < kMaxCopySize; ++copy_size) {
    for (size_t src_offset = 0; src_offset < kAlignBoundary; ++src_offset) {
      for (size_t dst_offset = 0; dst_offset < kAlignBoundary; ++dst_offset) {
        DoAndVerifyCopy(copy_size, src_offset, dst_offset);
      }
    }
  }
}
