// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-sd-emmc.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <threads.h>

#include <vector>

#include <hw/sdmmc.h>
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
                      .prefs = 0,
                  },
                  zx::interrupt(ZX_HANDLE_INVALID), ddk::GpioProtocolClient()) {
  }

  zx_status_t TestDdkAdd() {
    // call parent's bind
    return Bind();
  }

  void DdkRelease() {
    AmlSdEmmc::DdkRelease();
  }

  zx_status_t WaitForInterruptImpl() override {
    if (request_index_ < request_results_.size() && request_results_[request_index_] == 0) {
      // Indicate a receive CRC error.
      mmio_.Write32(1, kAmlSdEmmcStatusOffset);

      successful_transfers_ = 0;
      request_index_++;
    } else if (interrupt_status_.has_value()) {
      mmio_.Write32(interrupt_status_.value(), kAmlSdEmmcStatusOffset);
    } else {
      // Indicate that the request completed successfully.
      mmio_.Write32(1 << 13, kAmlSdEmmcStatusOffset);

      // Each tuning transfer is attempted five times with a short-circuit if one fails.
      // Report every successful transfer five times to make the results arrays easier to
      // follow.
      if (++successful_transfers_ % AML_SD_EMMC_TUNING_TEST_ATTEMPTS == 0) {
        successful_transfers_ = 0;
        request_index_++;
      }
    }
    return ZX_OK;
  }

  void WaitForBus() const override {
    /* Do nothing, bus is always ready in tests */
  }

  void SetRequestResults(const std::vector<uint8_t>& request_results) {
    request_results_ = request_results;
    request_index_ = 0;
  }

  void SetRequestInterruptStatus(uint32_t status) {
    interrupt_status_ = status;
  }

 private:
  std::vector<uint8_t> request_results_;
  size_t request_index_ = 0;
  uint32_t successful_transfers_ = 0;
  // The optional interrupt status to set after a request is completed.
  std::optional<uint32_t> interrupt_status_;
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

    dut_->set_board_config({
        .supports_dma = false,
        .min_freq = 400000,
        .max_freq = 120000000,
        .version_3 = true,
        .prefs = 0,
    });

    mmio_.Write32(0xff, kAmlSdEmmcDelay1Offset);
    mmio_.Write32(0xff, kAmlSdEmmcDelay2Offset);
    mmio_.Write32(0xff, kAmlSdEmmcAdjustOffset);

    dut_->SdmmcHwReset();

    EXPECT_EQ(mmio_.Read32(kAmlSdEmmcDelay1Offset), 0);
    EXPECT_EQ(mmio_.Read32(kAmlSdEmmcDelay2Offset), 0);
    EXPECT_EQ(mmio_.Read32(kAmlSdEmmcAdjustOffset), 0);

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
  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk.Ok());
}

TEST_F(AmlSdEmmcTest, TuningV3) {
  dut_->set_board_config({
      .supports_dma = false,
      .min_freq = 400000,
      .max_freq = 120000000,
      .version_3 = true,
      .prefs = 0,
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
}

TEST_F(AmlSdEmmcTest, TuningV2) {
  dut_->set_board_config({
      .supports_dma = false,
      .min_freq = 400000,
      .max_freq = 120000000,
      .version_3 = false,
      .prefs = 0,
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
}

TEST_F(AmlSdEmmcTest, TuningAllPass) {
  auto clock = AmlSdEmmcClock::Get().ReadFrom(&mmio_).set_cfg_div(10).WriteTo(&mmio_);
  auto adjust = AmlSdEmmcAdjust::Get().FromValue(0).set_adj_delay(0x3f).WriteTo(&mmio_);
  auto delay1 = AmlSdEmmcDelay1::Get().FromValue(0).WriteTo(&mmio_);
  auto delay2 = AmlSdEmmcDelay2::Get().FromValue(0).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());
  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  clock.ReadFrom(&mmio_);
  adjust.ReadFrom(&mmio_);
  delay1.ReadFrom(&mmio_);
  delay2.ReadFrom(&mmio_);

  EXPECT_EQ(clock.cfg_tx_phase(), 0);
  EXPECT_EQ(adjust.adj_delay(), 0);
  EXPECT_EQ(delay1.dly_0(), 32);
  EXPECT_EQ(delay1.dly_1(), 32);
  EXPECT_EQ(delay1.dly_2(), 32);
  EXPECT_EQ(delay1.dly_3(), 32);
  EXPECT_EQ(delay1.dly_4(), 32);
  EXPECT_EQ(delay2.dly_5(), 32);
  EXPECT_EQ(delay2.dly_6(), 32);
  EXPECT_EQ(delay2.dly_7(), 32);
  EXPECT_EQ(delay2.dly_8(), 32);
  EXPECT_EQ(delay2.dly_9(), 32);
}

TEST_F(AmlSdEmmcTest, AdjDelayTuningNoWindowWrap) {
  // clang-format off
  dut_->SetRequestResults({
    /*
    0  1  2  3  4  5  6  7  8  9
    */

    0, 0, 1, 1, 1, 1, 1, 1, 0, 0,  // Phase 0
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 1
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1,  // Phase 3
  });
  // clang-format on

  auto clock = AmlSdEmmcClock::Get().ReadFrom(&mmio_).set_cfg_div(10).WriteTo(&mmio_);
  auto adjust = AmlSdEmmcAdjust::Get().FromValue(0).set_adj_delay(0x3f).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());
  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  clock.ReadFrom(&mmio_);
  adjust.ReadFrom(&mmio_);

  EXPECT_EQ(clock.cfg_tx_phase(), 3);
  EXPECT_EQ(adjust.adj_delay(), 6);
}

TEST_F(AmlSdEmmcTest, AdjDelayTuningWindowWrap) {
  // clang-format off
  dut_->SetRequestResults({
    /*
    0  1  2  3  4  5  6  7  8  9
    */

    0, 1, 1, 0, 0, 1, 1, 1, 1, 0,  // Phase 0
    1, 1, 1, 0, 0, 0, 0, 1, 1, 1,  // Phase 1
    0, 0, 0, 1, 1, 1, 1, 1, 0, 0,  // Phase 3
  });
  // clang-format on

  auto clock = AmlSdEmmcClock::Get().ReadFrom(&mmio_).set_cfg_div(10).WriteTo(&mmio_);
  auto adjust = AmlSdEmmcAdjust::Get().FromValue(0).set_adj_delay(0x3f).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());
  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  clock.ReadFrom(&mmio_);
  adjust.ReadFrom(&mmio_);

  EXPECT_EQ(clock.cfg_tx_phase(), 1);
  EXPECT_EQ(adjust.adj_delay(), 0);
}

TEST_F(AmlSdEmmcTest, AdjDelayTuningAllFail) {
  // clang-format off
  dut_->SetRequestResults({
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  });
  // clang-format on

  AmlSdEmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());
  EXPECT_NOT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));
}

TEST_F(AmlSdEmmcTest, DelayLineTuningNoWindowWrap) {
  // clang-format off
  dut_->SetRequestResults({
    /*
     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63
    */

    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 0
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 1
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 2

    // Best window: start 12, size 10, delay 17.
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

  });
  // clang-format on

  AmlSdEmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);
  auto delay1 = AmlSdEmmcDelay1::Get().FromValue(0).WriteTo(&mmio_);
  auto delay2 = AmlSdEmmcDelay2::Get().FromValue(0).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());
  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  delay1.ReadFrom(&mmio_);
  delay2.ReadFrom(&mmio_);

  EXPECT_EQ(delay1.dly_0(), 17);
  EXPECT_EQ(delay1.dly_1(), 17);
  EXPECT_EQ(delay1.dly_2(), 17);
  EXPECT_EQ(delay1.dly_3(), 17);
  EXPECT_EQ(delay1.dly_4(), 17);
  EXPECT_EQ(delay2.dly_5(), 17);
  EXPECT_EQ(delay2.dly_6(), 17);
  EXPECT_EQ(delay2.dly_7(), 17);
  EXPECT_EQ(delay2.dly_8(), 17);
  EXPECT_EQ(delay2.dly_9(), 17);
}

TEST_F(AmlSdEmmcTest, DelayLineTuningWindowWrap) {
  // clang-format off
  dut_->SetRequestResults({
    /*
     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63
    */

    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 0
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 1
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 2

    // Best window: start 54, size 25, delay 2.
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  });
  // clang-format on

  AmlSdEmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);
  auto delay1 = AmlSdEmmcDelay1::Get().FromValue(0).WriteTo(&mmio_);
  auto delay2 = AmlSdEmmcDelay2::Get().FromValue(0).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());
  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  delay1.ReadFrom(&mmio_);
  delay2.ReadFrom(&mmio_);

  EXPECT_EQ(delay1.dly_0(), 2);
  EXPECT_EQ(delay1.dly_1(), 2);
  EXPECT_EQ(delay1.dly_2(), 2);
  EXPECT_EQ(delay1.dly_3(), 2);
  EXPECT_EQ(delay1.dly_4(), 2);
  EXPECT_EQ(delay2.dly_5(), 2);
  EXPECT_EQ(delay2.dly_6(), 2);
  EXPECT_EQ(delay2.dly_7(), 2);
  EXPECT_EQ(delay2.dly_8(), 2);
  EXPECT_EQ(delay2.dly_9(), 2);
}

TEST_F(AmlSdEmmcTest, DelayLineTuningAllFail) {
  // clang-format off
  dut_->SetRequestResults({

    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 0
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 1
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 2

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

  });
  // clang-format on

  AmlSdEmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());
  EXPECT_NOT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));
}

TEST_F(AmlSdEmmcTest, SetBusFreq) {
  ASSERT_OK(dut_->Init());

  auto clock = AmlSdEmmcClock::Get().FromValue(0).WriteTo(&mmio_);

  EXPECT_OK(dut_->SdmmcSetBusFreq(100'000'000));
  EXPECT_EQ(clock.ReadFrom(&mmio_).cfg_div(), 10);
  EXPECT_EQ(clock.cfg_src(), 1);

  EXPECT_OK(dut_->SdmmcSetBusFreq(200'000'000));
  EXPECT_EQ(clock.ReadFrom(&mmio_).cfg_div(), 9);
  EXPECT_EQ(clock.cfg_src(), 1);

  EXPECT_OK(dut_->SdmmcSetBusFreq(0));
  EXPECT_EQ(clock.ReadFrom(&mmio_).cfg_div(), 0);

  EXPECT_OK(dut_->SdmmcSetBusFreq(54'000'000));
  EXPECT_EQ(clock.ReadFrom(&mmio_).cfg_div(), 19);
  EXPECT_EQ(clock.cfg_src(), 1);

  EXPECT_OK(dut_->SdmmcSetBusFreq(400'000));
  EXPECT_EQ(clock.ReadFrom(&mmio_).cfg_div(), 60);
  EXPECT_EQ(clock.cfg_src(), 0);
}

TEST_F(AmlSdEmmcTest, ClearStatus) {
  ASSERT_OK(dut_->Init());

  // Set end_of_chain to indicate we're done and to have something to clear
  dut_->SetRequestInterruptStatus(1 << 13);
  sdmmc_req_t request;
  memset(&request, 0, sizeof(request));
  EXPECT_OK(dut_->SdmmcRequest(&request));

  auto status = AmlSdEmmcStatus::Get().FromValue(0);
  EXPECT_EQ(AmlSdEmmcStatus::kClearStatus, status.ReadFrom(&mmio_).reg_value());
}

TEST_F(AmlSdEmmcTest, TxCrcError) {
  ASSERT_OK(dut_->Init());

  // Set TX CRC error bit (8) and desc_busy bit (30)
  dut_->SetRequestInterruptStatus(1 << 8 | 1 << 30);
  sdmmc_req_t request;
  memset(&request, 0, sizeof(request));
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, dut_->SdmmcRequest(&request));

  auto start = AmlSdEmmcStart::Get().FromValue(0);
  // The desc busy bit should now have been cleared because of the error
  EXPECT_EQ(0, start.ReadFrom(&mmio_).desc_busy());
}

}  // namespace sdmmc
