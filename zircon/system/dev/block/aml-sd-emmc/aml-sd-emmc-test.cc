// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-sd-emmc.h"

#include <vector>

#include <hw/sdmmc.h>
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

  zx_status_t DdkRelease() {
    {
      fbl::AutoLock mutex_al(&mtx_);
      running_ = false;
    }

    return AmlSdEmmc::DdkRemove();
  }

  zx_status_t WaitForInterrupt() override {
    for (;;) {
      {
        fbl::AutoLock mutex_al(&mtx_);
        if (!running_) {
          return ZX_ERR_CANCELED;
        }
        if (cur_req_ != nullptr) {
          if (request_index_ < request_results_.size() && request_results_[request_index_++] == 0) {
            // Indicate a receive CRC error.
            mmio_.Write32(1, kAmlSdEmmcStatusOffset);
          } else {
            // Indicate that the request completed successfully.
            mmio_.Write32(1 << 13, kAmlSdEmmcStatusOffset);
          }

          return ZX_OK;
        }
      }

      zx::nanosleep(zx::deadline_after(zx::usec(100)));
    }
  }

  void SetRequestResults(const std::vector<uint8_t>& request_results) {
    request_results_ = request_results;
    request_index_ = 0;
  }

 private:
  std::vector<uint8_t> request_results_;
  size_t request_index_ = 0;
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

TEST_F(AmlSdEmmcTest, DelayTuningAllPass) {
  ASSERT_OK(dut_->Init());

  ASSERT_NO_FATAL_FAILURES(dut_->SetRequestResults({
    // clang-format off
    // Command tuning, 64 transfers for each RX clock phase.
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

    // Data tuning.
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    // clang-format on
  }));

  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  auto clock = AmlSdEmmcClock::Get().FromValue(0);
  auto delay1 = AmlSdEmmcDelay1::Get().FromValue(0);
  auto delay2 = AmlSdEmmcDelay2::Get().FromValue(0);

  EXPECT_EQ(clock.ReadFrom(&mmio_).cfg_rx_phase(), 0);
  EXPECT_EQ(delay2.ReadFrom(&mmio_).dly_9(), 32);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_0(), 32);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_1(), 32);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_2(), 32);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_3(), 32);
}

TEST_F(AmlSdEmmcTest, DelayTuningNoWindowWrap) {
  ASSERT_OK(dut_->Init());

  dut_->SetRequestResults({
    // clang-format off
    /*
     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63
    */

    // Best window: start 32, size 25, delay 44.
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1,

    // Best window: start 25, size 15, delay 32.
    1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1,

    // Best window: start 34, size 30, delay 49.
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

    // Best window: start 29, size 10, delay 34.
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,

    // Best window: start 12, size 10, delay 17.
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // clang-format on
  });

  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  auto clock = AmlSdEmmcClock::Get().FromValue(0);
  auto delay1 = AmlSdEmmcDelay1::Get().FromValue(0);
  auto delay2 = AmlSdEmmcDelay2::Get().FromValue(0);

  EXPECT_EQ(clock.ReadFrom(&mmio_).cfg_rx_phase(), 2);
  EXPECT_EQ(delay2.ReadFrom(&mmio_).dly_9(), 49);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_0(), 17);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_1(), 17);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_2(), 17);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_3(), 17);

}

TEST_F(AmlSdEmmcTest, DelayTuningWindowWrap) {
  ASSERT_OK(dut_->Init());

  dut_->SetRequestResults({
    // clang-format off
    /*
     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63
    */

    // Best window: start 19, size 15, delay 26.
    1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1,

    // Best window: start 0, size 18, delay 9.
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    // Best window: start 17, size 11, delay 22.
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,

    // Best window: start 49, size 19, delay 58.
    1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

    // Best window: start 54, size 25, delay 2.
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    // clang-format on
  });

  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  auto clock = AmlSdEmmcClock::Get().FromValue(0);
  auto delay1 = AmlSdEmmcDelay1::Get().FromValue(0);
  auto delay2 = AmlSdEmmcDelay2::Get().FromValue(0);

  EXPECT_EQ(clock.ReadFrom(&mmio_).cfg_rx_phase(), 3);
  EXPECT_EQ(delay2.ReadFrom(&mmio_).dly_9(), 58);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_0(), 2);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_1(), 2);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_2(), 2);
  EXPECT_EQ(delay1.ReadFrom(&mmio_).dly_3(), 2);
}

TEST_F(AmlSdEmmcTest, DelayTuningAllFail) {
  ASSERT_OK(dut_->Init());

  dut_->SetRequestResults({
    // clang-format off
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // clang-format on
  });

  EXPECT_NOT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));
}

}  // namespace sdmmc
