// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include "gtest/gtest.h"
#include "src/graphics/drivers/msd-img-rgx/mtk/mt8167s-gpu.h"

namespace {
struct fake_clock {
  bool enabled;
};

zx_status_t enable_clock(void* ctx) {
  reinterpret_cast<fake_clock*>(ctx)->enabled = true;
  return ZX_OK;
}

zx_status_t disable_clock(void* ctx) {
  reinterpret_cast<fake_clock*>(ctx)->enabled = false;
  return ZX_OK;
}

clock_protocol_ops_t fake_clock_ops = {
    .enable = enable_clock,
    .disable = disable_clock,
};

}  // namespace
class Mt8167GpuTest : public Mt8167sGpu {
 public:
  Mt8167GpuTest() : Mt8167sGpu(nullptr) {
    mmio_buffer_t power_buffer = {.vaddr = mock_power_gpu_registers_,
                                  .offset = 0,
                                  .size = sizeof(mock_power_gpu_registers_),
                                  .vmo = ZX_HANDLE_INVALID};
    power_gpu_buffer_ = ddk::MmioBuffer(power_buffer);
    mmio_buffer_t clock_buffer = {.vaddr = mock_clock_gpu_registers_,
                                  .offset = 0,
                                  .size = sizeof(mock_clock_gpu_registers_),
                                  .vmo = ZX_HANDLE_INVALID};
    clock_gpu_buffer_ = ddk::MmioBuffer(clock_buffer);
    mmio_buffer_t top_buffer = {.vaddr = mock_top_gpu_registers_,
                                .offset = 0,
                                .size = sizeof(mock_top_gpu_registers_),
                                .vmo = ZX_HANDLE_INVALID};
    gpu_buffer_ = ddk::MmioBuffer(top_buffer);
    for (uint32_t i = 0; i < clocks_.size(); ++i) {
      clock_protocol_t proto = {.ops = &fake_clock_ops, .ctx = &clocks_[i]};
      clks_[i] = ddk::ClockProtocolClient(&proto);
    }
  }

  void TestPowerDownMfgAsync() {
    clocks_[kClkAxiMfgIndex].enabled = true;
    clocks_[kClkSlowMfgIndex].enabled = true;
    EXPECT_EQ(ZX_OK, PowerDownMfgAsync());
    EXPECT_FALSE(clocks_[kClkAxiMfgIndex].enabled);
    EXPECT_FALSE(clocks_[kClkSlowMfgIndex].enabled);
  }

  void TestPowerDownMfg2d() {
    constexpr uint32_t kRegOffset = 0x2c0;
    constexpr uint32_t kPartialSramPdAck = (1 << 12);
    constexpr uint32_t kFullSramPdAck = (0xf << 12);
    mock_power_gpu_registers_[kRegOffset / 4] |= kPartialSramPdAck;
    // Waiting for RAM to power down should time out.
    EXPECT_EQ(ZX_ERR_TIMED_OUT, PowerDownMfg2d());
    mock_power_gpu_registers_[kRegOffset / 4] |= kFullSramPdAck;
    EXPECT_EQ(ZX_OK, PowerDownMfg2d());
  }

  void TestPowerDownMfg() {
    clocks_[kClkMfgMmIndex].enabled = true;
    constexpr uint32_t kRegOffset = 0x214;
    constexpr uint32_t kClockGateOffset = 0x4;
    constexpr uint32_t kClockGateValue = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);
    mock_power_gpu_registers_[kRegOffset / 4] = 0xffffffff;
    EXPECT_EQ(ZX_OK, PowerDownMfg());
    // The power register shouldn't be touched.
    EXPECT_EQ(0xffffffffu, mock_power_gpu_registers_[kRegOffset / 4]);
    EXPECT_EQ(kClockGateValue, mock_top_gpu_registers_[kClockGateOffset / 4]);
    clocks_[kClkMfgMmIndex].enabled = false;
  }

 private:
  static constexpr uint32_t kPowerRegionSize = 0x1000;
  static constexpr uint32_t kClockRegionSize = 0x2000;
  static constexpr uint32_t kTopRegionSize = 0x1000;
  uint32_t mock_power_gpu_registers_[kPowerRegionSize / sizeof(uint32_t)] = {};
  uint32_t mock_clock_gpu_registers_[kClockRegionSize / sizeof(uint32_t)] = {};
  uint32_t mock_top_gpu_registers_[kTopRegionSize / sizeof(uint32_t)] = {};
  std::array<fake_clock, kClockCount> clocks_;
};

TEST(Mt8167Gpu, PowerDownMfgAsync) {
  Mt8167GpuTest test_gpu;
  test_gpu.TestPowerDownMfgAsync();
};

TEST(Mt8167Gpu, PowerDownMfg2d) {
  Mt8167GpuTest test_gpu;
  test_gpu.TestPowerDownMfg2d();
};

TEST(Mt8167Gpu, PowerDownMfg) {
  Mt8167GpuTest test_gpu;
  test_gpu.TestPowerDownMfg();
};
