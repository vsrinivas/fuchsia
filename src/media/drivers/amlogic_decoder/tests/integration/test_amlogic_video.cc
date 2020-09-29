// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

#include "amlogic-video.h"
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

    // While we are here testing InternalBuffer's, verify the duplicate API
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, internal_buffer2.value().vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    ASSERT_TRUE(dup.is_valid());

    video.reset();
  }

  static void LoadFirmware(bool vdec) {
    auto firmware_type = vdec ? FirmwareBlob::FirmwareType::kDec_H264_Multi_Gxm
                              : FirmwareBlob::FirmwareType::kDec_Vp9_G12a;
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    uint8_t* data;
    uint32_t firmware_size;
    ASSERT_EQ(ZX_OK, video->firmware_blob()->GetFirmwareData(firmware_type, &data, &firmware_size));
    DecoderCore* core = vdec ? video->vdec1_core() : video->hevc_core();
    EXPECT_TRUE(core->LoadFirmwareToBuffer(data, firmware_size).has_value());
  }
};

TEST(AmlogicVideo, BufferAlignment) { TestAmlogicVideo::BufferAlignment(); }

TEST(AmlogicVideo, LoadVdecFirmware) { TestAmlogicVideo::LoadFirmware(/*vdec=*/true); }

TEST(AmlogicVideo, LoadHeavcFirmware) { TestAmlogicVideo::LoadFirmware(/*vdec=*/false); }
