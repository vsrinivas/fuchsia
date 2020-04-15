// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include <fbl/algorithm.h>

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "tests/test_support.h"

class TestAmlogicVideo {
 public:
  static void BufferAlignment() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    constexpr uint32_t kBufferSize = 4096;
    // Try to force the second buffer to be misaligned.
    constexpr uint32_t kFirstAlignment = 1u << 13;
    auto internal_buffer = InternalBuffer::CreateAligned(
        "TestBuffer1", &video->SysmemAllocatorSyncPtr(), video->bti(), kBufferSize, kFirstAlignment,
        /*is_secure*/ false, /*is_writable=*/true, /*is_mapping_needed=*/false);
    ASSERT_TRUE(internal_buffer.is_ok());
    EXPECT_EQ(fbl::round_up(internal_buffer.value().phys_base(), kFirstAlignment),
              internal_buffer.value().phys_base());

    // Should be larger than first.
    constexpr uint32_t kSecondAlignment = 1u << 16;
    auto internal_buffer2 = InternalBuffer::CreateAligned(
        "TestBuffer2", &video->SysmemAllocatorSyncPtr(), video->bti(), kBufferSize,
        kSecondAlignment, /*is_secure*/ false, /*is_writable=*/true, /*is_mapping_needed=*/false);
    ASSERT_TRUE(internal_buffer2.is_ok());
    EXPECT_EQ(fbl::round_up(internal_buffer2.value().phys_base(), kSecondAlignment),
              internal_buffer2.value().phys_base());
    video.reset();
  }
};

TEST(AmlogicVideo, BufferAlignment) { TestAmlogicVideo::BufferAlignment(); }
