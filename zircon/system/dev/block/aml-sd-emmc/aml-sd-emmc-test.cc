// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-sd-emmc.h"

#include <vector>

#include <lib/fake_ddk/fake_ddk.h>
#include <soc/aml-s912/s912-hw.h>
#include <zxtest/zxtest.h>

namespace sdmmc {

class TestAmlSdEmmc : public AmlSdEmmc {
 public:
  explicit TestAmlSdEmmc(const mmio_buffer_t& mmio)
      : AmlSdEmmc(fake_ddk::kFakeParent, zx::bti(ZX_HANDLE_INVALID), ddk::MmioBuffer(mmio),
                  ddk::MmioPinnedBuffer({&mmio, ZX_HANDLE_INVALID, 0x100}),
                  aml_sd_emmc_config_t{
                      .supports_dma = false,
                      .min_freq = 400000,
                      .max_freq = 120000000,
                      .clock_phases =
                          {
                              .init = {.core_phase = 3, .tx_phase = 0},
                              .hs = {.core_phase = 1, .tx_phase = 0},
                              .legacy = {.core_phase = 1, .tx_phase = 2},
                              .ddr = {.core_phase = 2, .tx_phase = 0},
                              .hs2 = {.core_phase = 3, .tx_phase = 0},
                              .hs4 = {.core_phase = 0, .tx_phase = 0},
                              .sdr104 = {.core_phase = 2, .tx_phase = 0},
                          },
                  },
                  zx::interrupt(ZX_HANDLE_INVALID), ddk::GpioProtocolClient()) {}
  zx_status_t TestDdkAdd() {
    // call parent's bind
    return Bind();
  }
};

class AmlSdEmmcTest : public zxtest::Test {
 public:
  AmlSdEmmcTest() : mmio_({&mmio_, 0, 0, ZX_HANDLE_INVALID}) {}

  void SetUp() override {
    registers_.reset(new uint8_t[S912_SD_EMMC_B_LENGTH]);
    memset(registers_.get(), 0, S912_SD_EMMC_B_LENGTH);

    mmio_buffer_t mmio_buffer = {
        .vaddr = registers_.get(),
        .offset = 0,
        .size = S912_SD_EMMC_B_LENGTH,
        .vmo = ZX_HANDLE_INVALID,
    };

    mmio_ = ddk::MmioBuffer(mmio_buffer);
    dut_ = new TestAmlSdEmmc(mmio_buffer);
  }

  void TearDown() override {
    if (dut_ != nullptr) {
      dut_->DdkRelease();
    }
  }

 protected:
  ddk::MmioBuffer mmio_;
  TestAmlSdEmmc* dut_ = nullptr;

 private:
  std::unique_ptr<uint8_t[]> registers_;
};

TEST_F(AmlSdEmmcTest, DdkLifecycle) {
  fake_ddk::Bind ddk;
  EXPECT_OK(dut_->TestDdkAdd());
  dut_->DdkUnbind();
  EXPECT_TRUE(ddk.Ok());
}

TEST_F(AmlSdEmmcTest, SetClockPhase) {
  EXPECT_OK(dut_->SdmmcSetTiming(SDMMC_TIMING_HS200));
  EXPECT_EQ(mmio_.Read32(0), (3 << 8) | (0 << 10));

  mmio_.Write32(0, 0);

  EXPECT_OK(dut_->SdmmcSetTiming(SDMMC_TIMING_LEGACY));
  EXPECT_EQ(mmio_.Read32(0), (1 << 8) | (2 << 10));
}

}  // namespace sdmmc
