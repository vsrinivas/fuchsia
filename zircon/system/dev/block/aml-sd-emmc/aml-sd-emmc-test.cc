// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-sd-emmc.h"

#include <vector>

#include <hw/sdmmc.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <soc/aml-s912/s912-hw.h>
#include <zxtest/zxtest.h>

#include "aml-sd-emmc-regs.h"

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
                      .version_3 = true,
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

  void DdkRelease() {
    {
      fbl::AutoLock mutex_al(&mtx_);
      running_ = false;
    }

    AmlSdEmmc::DdkRelease();
  }

  zx_status_t WaitForInterrupt() override {
    for (;;) {
      {
        fbl::AutoLock mutex_al(&mtx_);

        if (!running_) {
          return ZX_ERR_CANCELED;
        }
        if (cur_req_ != nullptr) {
          // Indicate that the request completed successfully.
          mmio_.Write32(1 << 13, kAmlSdEmmcStatusOffset);
          return ZX_OK;
        }
      }

      zx::nanosleep(zx::deadline_after(zx::usec(1)));
    }
  }

 private:
  bool running_ TA_GUARDED(mtx_) = true;
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

    mmio_.Write32(1, kAmlSdEmmcCfgOffset);  // Set bus width 4.
    memcpy(reinterpret_cast<uint8_t*>(mmio_.get()) + kAmlSdEmmcPingOffset,
           aml_sd_emmc_tuning_blk_pattern_4bit, sizeof(aml_sd_emmc_tuning_blk_pattern_4bit));
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
  dut_->DdkUnbindDeprecated();
  EXPECT_TRUE(ddk.Ok());
}

TEST_F(AmlSdEmmcTest, SetClockPhase) {
  EXPECT_OK(dut_->SdmmcSetTiming(SDMMC_TIMING_HS200));
  EXPECT_EQ(mmio_.Read32(0), (3 << 8) | (0 << 10));

  mmio_.Write32(0, 0);

  EXPECT_OK(dut_->SdmmcSetTiming(SDMMC_TIMING_LEGACY));
  EXPECT_EQ(mmio_.Read32(0), (1 << 8) | (2 << 10));
}

TEST_F(AmlSdEmmcTest, TuningV3) {
  dut_->set_board_config({
      .supports_dma = false,
      .min_freq = 400000,
      .max_freq = 120000000,
      .version_3 = true,
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
  });

  AmlSdEmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);

  auto adjust = AmlSdEmmcAdjust::Get().FromValue(0);
  auto adjust_v2 = AmlSdEmmcAdjustV2::Get().FromValue(0);

  adjust.set_adj_fixed(0).set_adj_delay(0x3f).WriteTo(&mmio_);
  adjust_v2.set_adj_fixed(0).set_adj_delay(0x3f).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());
  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  adjust.ReadFrom(&mmio_);
  adjust_v2.ReadFrom(&mmio_);

  EXPECT_EQ(adjust.adj_fixed(), 1);
  EXPECT_EQ(adjust.adj_delay(), 0);
  EXPECT_EQ(adjust_v2.adj_fixed(), 0);
  EXPECT_EQ(adjust_v2.adj_delay(), 0x3f);
}

TEST_F(AmlSdEmmcTest, TuningV2) {
  dut_->set_board_config({
      .supports_dma = false,
      .min_freq = 400000,
      .max_freq = 120000000,
      .version_3 = false,
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
  });

  AmlSdEmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);

  auto adjust = AmlSdEmmcAdjust::Get().FromValue(0);
  auto adjust_v2 = AmlSdEmmcAdjustV2::Get().FromValue(0);

  adjust.set_adj_fixed(0).set_adj_delay(0x3f).WriteTo(&mmio_);
  adjust_v2.set_adj_fixed(0).set_adj_delay(0x3f).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());
  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  adjust.ReadFrom(&mmio_);
  adjust_v2.ReadFrom(&mmio_);

  EXPECT_EQ(adjust_v2.adj_fixed(), 1);
  EXPECT_EQ(adjust_v2.adj_delay(), 0);
  EXPECT_EQ(adjust.adj_fixed(), 0);
  EXPECT_EQ(adjust.adj_delay(), 0x3f);
}

}  // namespace sdmmc
