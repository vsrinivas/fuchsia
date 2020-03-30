// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-gpu.h"

#include <lib/zx/vmo.h>

#include <zxtest/zxtest.h>

#include "s905d2-gpu.h"

namespace aml_gpu {

class TestAmlGpu {
 public:
  static void TestSetClkFreq() {
    aml_gpu::AmlGpu aml_gpu(nullptr);
    aml_gpu.gpu_block_ = &s905d2_gpu_blocks;
    zx::vmo vmo;
    constexpr uint32_t kHiuRegisterSize = 1024 * 16;
    ASSERT_OK(zx::vmo::create(kHiuRegisterSize, 0, &vmo));
    ASSERT_OK(ddk::MmioBuffer::Create(0, kHiuRegisterSize, std::move(vmo), ZX_CACHE_POLICY_CACHED,
                                      &aml_gpu.hiu_buffer_));
    aml_gpu.SetClkFreqSource(1);
    uint32_t value = aml_gpu.hiu_buffer_->Read32(0x6c << 2);
    // Mux should be set to 1.
    EXPECT_EQ(1, value >> kFinalMuxBitShift);
    uint32_t parent_mux_value = (value >> 16) & 0xfff;
    uint32_t source = parent_mux_value >> 9;
    bool enabled = (parent_mux_value >> kClkEnabledBitShift) & 1;
    uint32_t divisor = (parent_mux_value & 0xff) + 1;
    EXPECT_EQ(S905D2_FCLK_DIV5, source);
    EXPECT_TRUE(enabled);
    EXPECT_EQ(1, divisor);
  }
};
}  // namespace aml_gpu

TEST(AmlGpu, SetClkFreq) { aml_gpu::TestAmlGpu::TestSetClkFreq(); }
