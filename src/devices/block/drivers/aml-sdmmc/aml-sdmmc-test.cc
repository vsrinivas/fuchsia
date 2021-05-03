// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-sdmmc.h"

#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio-ptr/fake.h>
#include <lib/sdio/hw.h>
#include <lib/sdmmc/hw.h>
#include <threads.h>

#include <vector>

#include <soc/aml-s912/s912-hw.h>
#include <zxtest/zxtest.h>

#include "aml-sdmmc-regs.h"

namespace sdmmc {

class TestAmlSdmmc : public AmlSdmmc {
 public:
  TestAmlSdmmc(const mmio_buffer_t& mmio, zx::bti bti)
      : AmlSdmmc(fake_ddk::kFakeParent, std::move(bti), ddk::MmioBuffer(mmio),
                 ddk::MmioPinnedBuffer({&mmio, ZX_HANDLE_INVALID, 0x100}),
                 aml_sdmmc_config_t{
                     .supports_dma = true,
                     .min_freq = 400000,
                     .max_freq = 120000000,
                     .version_3 = true,
                     .prefs = 0,
                 },
                 zx::interrupt(ZX_HANDLE_INVALID), ddk::GpioProtocolClient()) {}

  zx_status_t TestDdkAdd() {
    // call parent's bind
    return Bind();
  }

  void DdkRelease() { AmlSdmmc::DdkRelease(); }

  zx_status_t WaitForInterruptImpl() override {
    if (request_index_ < request_results_.size() && request_results_[request_index_] == 0) {
      // Indicate a receive CRC error.
      mmio_.Write32(1, kAmlSdmmcStatusOffset);

      successful_transfers_ = 0;
      request_index_++;
    } else if (interrupt_status_.has_value()) {
      mmio_.Write32(interrupt_status_.value(), kAmlSdmmcStatusOffset);
    } else {
      // Indicate that the request completed successfully.
      mmio_.Write32(1 << 13, kAmlSdmmcStatusOffset);

      // Each tuning transfer is attempted five times with a short-circuit if one fails.
      // Report every successful transfer five times to make the results arrays easier to
      // follow.
      if (++successful_transfers_ % AML_SDMMC_TUNING_TEST_ATTEMPTS == 0) {
        successful_transfers_ = 0;
        request_index_++;
      }
    }
    return ZX_OK;
  }

  void WaitForBus() const override { /* Do nothing, bus is always ready in tests */
  }

  void SetRequestResults(const std::vector<uint8_t>& request_results) {
    request_results_ = request_results;
    request_index_ = 0;
  }

  void SetRequestInterruptStatus(uint32_t status) { interrupt_status_ = status; }

  aml_sdmmc_desc_t* descs() { return AmlSdmmc::descs(); }

 private:
  std::vector<uint8_t> request_results_;
  size_t request_index_ = 0;
  uint32_t successful_transfers_ = 0;
  // The optional interrupt status to set after a request is completed.
  std::optional<uint32_t> interrupt_status_;
};

class AmlSdmmcTest : public zxtest::Test {
 public:
  AmlSdmmcTest() : mmio_({FakeMmioPtr(&mmio_), 0, 0, ZX_HANDLE_INVALID}) {}

  void SetUp() override {
    registers_.reset(new uint8_t[S912_SD_EMMC_B_LENGTH]);
    memset(registers_.get(), 0, S912_SD_EMMC_B_LENGTH);

    mmio_buffer_t mmio_buffer = {
        .vaddr = FakeMmioPtr(registers_.get()),
        .offset = 0,
        .size = S912_SD_EMMC_B_LENGTH,
        .vmo = ZX_HANDLE_INVALID,
    };

    mmio_ = ddk::MmioBuffer(mmio_buffer);

    memset(bti_paddrs_, 0, sizeof(bti_paddrs_));
    bti_paddrs_[0] = PAGE_SIZE;  // This is passed to AmlSdmmc::Init().

    zx::bti bti;
    ASSERT_OK(fake_bti_create_with_paddrs(bti_paddrs_, countof(bti_paddrs_),
                                          bti.reset_and_get_address()));

    dut_ = new TestAmlSdmmc(mmio_buffer, std::move(bti));

    dut_->set_board_config({
        .supports_dma = true,
        .min_freq = 400000,
        .max_freq = 120000000,
        .version_3 = true,
        .prefs = 0,
    });

    mmio_.Write32(0xff, kAmlSdmmcDelay1Offset);
    mmio_.Write32(0xff, kAmlSdmmcDelay2Offset);
    mmio_.Write32(0xff, kAmlSdmmcAdjustOffset);

    dut_->SdmmcHwReset();

    EXPECT_EQ(mmio_.Read32(kAmlSdmmcDelay1Offset), 0);
    EXPECT_EQ(mmio_.Read32(kAmlSdmmcDelay2Offset), 0);
    EXPECT_EQ(mmio_.Read32(kAmlSdmmcAdjustOffset), 0);

    mmio_.Write32(1, kAmlSdmmcCfgOffset);  // Set bus width 4.
    memcpy(reinterpret_cast<uint8_t*>(registers_.get()) + kAmlSdmmcPingOffset,
           aml_sdmmc_tuning_blk_pattern_4bit, sizeof(aml_sdmmc_tuning_blk_pattern_4bit));
  }

  void TearDown() override {
    if (dut_ != nullptr) {
      dut_->DdkRelease();
    }
  }

 protected:
  static zx_koid_t GetVmoKoid(const zx::vmo& vmo) {
    zx_info_handle_basic_t info = {};
    size_t actual = 0;
    size_t available = 0;
    zx_status_t status =
        vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), &actual, &available);
    if (status != ZX_OK || actual < 1) {
      return ZX_KOID_INVALID;
    }
    return info.koid;
  }

  void InitializeContiguousPaddrs(const size_t vmos) {
    // Start at 1 because one paddr has already been read to create the DMA descriptor buffer.
    for (size_t i = 0; i < vmos; i++) {
      bti_paddrs_[i + 1] = (i << 24) | PAGE_SIZE;
    }
  }

  void InitializeSingleVmoPaddrs(const size_t pages) {
    // Start at 1 because one paddr has already been read to create the DMA descriptor buffer.
    for (size_t i = 0; i < pages; i++) {
      bti_paddrs_[i + 1] = PAGE_SIZE * (i + 1);
    }
  }

  void InitializeNonContiguousPaddrs(const size_t vmos) {
    for (size_t i = 0; i < vmos; i++) {
      bti_paddrs_[i + 1] = PAGE_SIZE * (i + 1) * 2;
    }
  }

  zx_paddr_t bti_paddrs_[64] = {};

  ddk::MmioBuffer mmio_;
  TestAmlSdmmc* dut_ = nullptr;

 private:
  std::unique_ptr<uint8_t[]> registers_;
};

TEST_F(AmlSdmmcTest, DdkLifecycle) {
  fake_ddk::Bind ddk;
  EXPECT_OK(dut_->TestDdkAdd());
  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk.Ok());
}

TEST_F(AmlSdmmcTest, InitV3) {
  dut_->set_board_config({
      .supports_dma = false,
      .min_freq = 400000,
      .max_freq = 120000000,
      .version_3 = true,
      .prefs = 0,
  });

  AmlSdmmcClock::Get().FromValue(0).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());

  EXPECT_EQ(AmlSdmmcClock::Get().ReadFrom(&mmio_).reg_value(), AmlSdmmcClockV3::Get()
                                                                   .FromValue(0)
                                                                   .set_cfg_div(60)
                                                                   .set_cfg_src(0)
                                                                   .set_cfg_co_phase(2)
                                                                   .set_cfg_tx_phase(0)
                                                                   .set_cfg_rx_phase(0)
                                                                   .set_cfg_always_on(1)
                                                                   .reg_value());
}

TEST_F(AmlSdmmcTest, InitV2) {
  dut_->set_board_config({
      .supports_dma = false,
      .min_freq = 400000,
      .max_freq = 120000000,
      .version_3 = false,
      .prefs = 0,
  });

  AmlSdmmcClock::Get().FromValue(0).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());

  EXPECT_EQ(AmlSdmmcClock::Get().ReadFrom(&mmio_).reg_value(), AmlSdmmcClockV2::Get()
                                                                   .FromValue(0)
                                                                   .set_cfg_div(60)
                                                                   .set_cfg_src(0)
                                                                   .set_cfg_co_phase(2)
                                                                   .set_cfg_tx_phase(0)
                                                                   .set_cfg_rx_phase(0)
                                                                   .set_cfg_always_on(1)
                                                                   .reg_value());
}

TEST_F(AmlSdmmcTest, TuningV3) {
  dut_->set_board_config({
      .supports_dma = false,
      .min_freq = 400000,
      .max_freq = 120000000,
      .version_3 = true,
      .prefs = 0,
  });

  ASSERT_OK(dut_->Init());

  AmlSdmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);
  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_bus_width(AmlSdmmcCfg::kBusWidth4Bit).WriteTo(&mmio_);

  auto adjust = AmlSdmmcAdjust::Get().FromValue(0);
  auto adjust_v2 = AmlSdmmcAdjustV2::Get().FromValue(0);

  adjust.set_adj_fixed(0).set_adj_delay(0x3f).WriteTo(&mmio_);
  adjust_v2.set_adj_fixed(0).set_adj_delay(0x3f).WriteTo(&mmio_);

  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  adjust.ReadFrom(&mmio_);
  adjust_v2.ReadFrom(&mmio_);

  EXPECT_EQ(adjust.adj_fixed(), 1);
  EXPECT_EQ(adjust.adj_delay(), 0);
}

TEST_F(AmlSdmmcTest, TuningV2) {
  dut_->set_board_config({
      .supports_dma = false,
      .min_freq = 400000,
      .max_freq = 120000000,
      .version_3 = false,
      .prefs = 0,
  });

  ASSERT_OK(dut_->Init());

  AmlSdmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);
  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_bus_width(AmlSdmmcCfg::kBusWidth4Bit).WriteTo(&mmio_);

  auto adjust = AmlSdmmcAdjust::Get().FromValue(0);
  auto adjust_v2 = AmlSdmmcAdjustV2::Get().FromValue(0);

  adjust.set_adj_fixed(0).set_adj_delay(0x3f).WriteTo(&mmio_);
  adjust_v2.set_adj_fixed(0).set_adj_delay(0x3f).WriteTo(&mmio_);

  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  adjust.ReadFrom(&mmio_);
  adjust_v2.ReadFrom(&mmio_);

  EXPECT_EQ(adjust_v2.adj_fixed(), 1);
  EXPECT_EQ(adjust_v2.adj_delay(), 0);
}

TEST_F(AmlSdmmcTest, TuningAllPass) {
  ASSERT_OK(dut_->Init());

  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_bus_width(AmlSdmmcCfg::kBusWidth4Bit).WriteTo(&mmio_);

  auto clock = AmlSdmmcClock::Get().ReadFrom(&mmio_).set_cfg_div(10).WriteTo(&mmio_);
  auto adjust = AmlSdmmcAdjust::Get().FromValue(0).set_adj_delay(0x3f).WriteTo(&mmio_);
  auto delay1 = AmlSdmmcDelay1::Get().FromValue(0).WriteTo(&mmio_);
  auto delay2 = AmlSdmmcDelay2::Get().FromValue(0).WriteTo(&mmio_);

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

TEST_F(AmlSdmmcTest, AdjDelayTuningNoWindowWrap) {
  // clang-format off
  dut_->SetRequestResults({
    /*
    0  1  2  3  4  5  6  7  8  9
    */

    0, 0, 1, 1, 1, 1, 1, 1, 0, 0,  // Phase 0
    0, 0, 0, 1, 1, 1, 0, 0, 0, 0,  // Phase 1
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1,  // Phase 3
  });
  // clang-format on

  ASSERT_OK(dut_->Init());

  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_bus_width(AmlSdmmcCfg::kBusWidth4Bit).WriteTo(&mmio_);

  auto clock = AmlSdmmcClock::Get().ReadFrom(&mmio_).set_cfg_div(10).WriteTo(&mmio_);
  auto adjust = AmlSdmmcAdjust::Get().FromValue(0).set_adj_delay(0x3f).WriteTo(&mmio_);

  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  clock.ReadFrom(&mmio_);
  adjust.ReadFrom(&mmio_);

  EXPECT_EQ(clock.cfg_tx_phase(), 3);
  EXPECT_EQ(adjust.adj_delay(), 6);
}

TEST_F(AmlSdmmcTest, AdjDelayTuningLargestWindowChosen) {
  // clang-format off
  dut_->SetRequestResults({
    /*
    0  1  2  3  4  5  6  7  8  9
    */

    0, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 0
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 1
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1,  // Phase 3
  });
  // clang-format on

  ASSERT_OK(dut_->Init());

  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_bus_width(AmlSdmmcCfg::kBusWidth4Bit).WriteTo(&mmio_);

  auto clock = AmlSdmmcClock::Get().ReadFrom(&mmio_).set_cfg_div(10).WriteTo(&mmio_);
  auto adjust = AmlSdmmcAdjust::Get().FromValue(0).set_adj_delay(0x3f).WriteTo(&mmio_);

  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  clock.ReadFrom(&mmio_);
  adjust.ReadFrom(&mmio_);

  EXPECT_EQ(clock.cfg_tx_phase(), 1);
  EXPECT_EQ(adjust.adj_delay(), 0);
}

TEST_F(AmlSdmmcTest, AdjDelayTuningWindowWrap) {
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

  ASSERT_OK(dut_->Init());

  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_bus_width(AmlSdmmcCfg::kBusWidth4Bit).WriteTo(&mmio_);

  auto clock = AmlSdmmcClock::Get().ReadFrom(&mmio_).set_cfg_div(10).WriteTo(&mmio_);
  auto adjust = AmlSdmmcAdjust::Get().FromValue(0).set_adj_delay(0x3f).WriteTo(&mmio_);

  EXPECT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));

  clock.ReadFrom(&mmio_);
  adjust.ReadFrom(&mmio_);

  EXPECT_EQ(clock.cfg_tx_phase(), 1);
  EXPECT_EQ(adjust.adj_delay(), 0);
}

TEST_F(AmlSdmmcTest, AdjDelayTuningAllFail) {
  // clang-format off
  dut_->SetRequestResults({
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  });
  // clang-format on

  ASSERT_OK(dut_->Init());

  AmlSdmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);
  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_bus_width(AmlSdmmcCfg::kBusWidth4Bit).WriteTo(&mmio_);

  EXPECT_NOT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));
}

TEST_F(AmlSdmmcTest, DelayLineTuningNoWindowWrap) {
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

  ASSERT_OK(dut_->Init());

  AmlSdmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);
  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_bus_width(AmlSdmmcCfg::kBusWidth4Bit).WriteTo(&mmio_);
  auto delay1 = AmlSdmmcDelay1::Get().FromValue(0).WriteTo(&mmio_);
  auto delay2 = AmlSdmmcDelay2::Get().FromValue(0).WriteTo(&mmio_);

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

TEST_F(AmlSdmmcTest, DelayLineTuningWindowWrap) {
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

  ASSERT_OK(dut_->Init());

  AmlSdmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);
  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_bus_width(AmlSdmmcCfg::kBusWidth4Bit).WriteTo(&mmio_);
  auto delay1 = AmlSdmmcDelay1::Get().FromValue(0).WriteTo(&mmio_);
  auto delay2 = AmlSdmmcDelay2::Get().FromValue(0).WriteTo(&mmio_);

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

TEST_F(AmlSdmmcTest, DelayLineTuningAllFail) {
  // clang-format off
  dut_->SetRequestResults({

    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 0
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 1
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // Phase 2

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

  });
  // clang-format on

  AmlSdmmcClock::Get().FromValue(0).set_cfg_div(10).WriteTo(&mmio_);

  ASSERT_OK(dut_->Init());
  EXPECT_NOT_OK(dut_->SdmmcPerformTuning(SD_SEND_TUNING_BLOCK));
}

TEST_F(AmlSdmmcTest, SetBusFreq) {
  ASSERT_OK(dut_->Init());

  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_bus_width(AmlSdmmcCfg::kBusWidth4Bit).WriteTo(&mmio_);

  auto clock = AmlSdmmcClock::Get().FromValue(0).WriteTo(&mmio_);

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

TEST_F(AmlSdmmcTest, ClearStatus) {
  ASSERT_OK(dut_->Init());

  // Set end_of_chain to indicate we're done and to have something to clear
  dut_->SetRequestInterruptStatus(1 << 13);
  sdmmc_req_t request;
  memset(&request, 0, sizeof(request));
  EXPECT_OK(dut_->SdmmcRequest(&request));

  auto status = AmlSdmmcStatus::Get().FromValue(0);
  EXPECT_EQ(AmlSdmmcStatus::kClearStatus, status.ReadFrom(&mmio_).reg_value());
}

TEST_F(AmlSdmmcTest, TxCrcError) {
  ASSERT_OK(dut_->Init());

  // Set TX CRC error bit (8) and desc_busy bit (30)
  dut_->SetRequestInterruptStatus(1 << 8 | 1 << 30);
  sdmmc_req_t request;
  memset(&request, 0, sizeof(request));
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, dut_->SdmmcRequest(&request));

  auto start = AmlSdmmcStart::Get().FromValue(0);
  // The desc busy bit should now have been cleared because of the error
  EXPECT_EQ(0, start.ReadFrom(&mmio_).desc_busy());
}

TEST_F(AmlSdmmcTest, RequestsFailAfterSuspend) {
  ASSERT_OK(dut_->Init());

  sdmmc_req_t request;
  memset(&request, 0, sizeof(request));
  EXPECT_OK(dut_->SdmmcRequest(&request));

  ddk::SuspendTxn txn(fake_ddk::kFakeDevice, 0, false, 0);
  dut_->DdkSuspend(std::move(txn));

  EXPECT_NOT_OK(dut_->SdmmcRequest(&request));
}

TEST_F(AmlSdmmcTest, UnownedVmosBlockMode) {
  ASSERT_OK(dut_->Init());

  InitializeContiguousPaddrs(10);

  zx::vmo vmos[10] = {};
  sdmmc_buffer_region_t buffers[10];
  for (uint32_t i = 0; i < countof(vmos); i++) {
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmos[i]));
    buffers[i] = {
        .buffer =
            {
                .vmo = vmos[i].get(),
            },
        .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
        .offset = i * 16,
        .size = 32 * (i + 2),
    };
  }

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = buffers,
      .buffers_count = countof(buffers),
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(2)
                               .set_block_mode(1)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, PAGE_SIZE);
  EXPECT_EQ(descs[0].resp_addr, 0);

  for (uint32_t i = 1; i < countof(vmos); i++) {
    expected_desc_cfg.set_len(i + 2).set_no_resp(1).set_no_cmd(1).set_resp_num(0).set_cmd_idx(0);
    if (i == countof(vmos) - 1) {
      expected_desc_cfg.set_end_of_chain(1);
    }
    EXPECT_EQ(descs[i].cmd_info, expected_desc_cfg.reg_value());
    EXPECT_EQ(descs[i].cmd_arg, 0);
    EXPECT_EQ(descs[i].data_addr, (i << 24) | (PAGE_SIZE + (i * 16)));
    EXPECT_EQ(descs[i].resp_addr, 0);
  }
}

TEST_F(AmlSdmmcTest, UnownedVmosNotBlockSizeMultiple) {
  ASSERT_OK(dut_->Init());

  InitializeContiguousPaddrs(10);

  zx::vmo vmos[10] = {};
  sdmmc_buffer_region_t buffers[10];
  for (uint32_t i = 0; i < countof(vmos); i++) {
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmos[i]));
    buffers[i] = {
        .buffer =
            {
                .vmo = vmos[i].get(),
            },
        .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
        .offset = 0,
        .size = 32 * (i + 2),
    };
  }

  buffers[5].size = 25;

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = buffers,
      .buffers_count = countof(buffers),
  };
  uint32_t response[4] = {};
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
}

TEST_F(AmlSdmmcTest, UnownedVmosByteMode) {
  ASSERT_OK(dut_->Init());

  InitializeContiguousPaddrs(10);

  zx::vmo vmos[10] = {};
  sdmmc_buffer_region_t buffers[10];
  for (uint32_t i = 0; i < countof(vmos); i++) {
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmos[i]));
    buffers[i] = {
        .buffer =
            {
                .vmo = vmos[i].get(),
            },
        .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
        .offset = i * 4,
        .size = 50,
    };
  }

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 50,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = buffers,
      .buffers_count = countof(buffers),
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(50)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, PAGE_SIZE);
  EXPECT_EQ(descs[0].resp_addr, 0);

  for (uint32_t i = 1; i < countof(vmos); i++) {
    expected_desc_cfg.set_len(50).set_no_resp(1).set_no_cmd(1).set_resp_num(0).set_cmd_idx(0);
    if (i == countof(vmos) - 1) {
      expected_desc_cfg.set_end_of_chain(1);
    }
    EXPECT_EQ(descs[i].cmd_info, expected_desc_cfg.reg_value());
    EXPECT_EQ(descs[i].cmd_arg, 0);
    EXPECT_EQ(descs[i].data_addr, (i << 24) | (PAGE_SIZE + (i * 4)));
    EXPECT_EQ(descs[i].resp_addr, 0);
  }
}

TEST_F(AmlSdmmcTest, UnownedVmoByteModeMultiBlock) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
  InitializeContiguousPaddrs(1);

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 0,
      .size = 400,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 100,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(100)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, PAGE_SIZE);
  EXPECT_EQ(descs[0].resp_addr, 0);

  for (uint32_t i = 1; i < 4; i++) {
    expected_desc_cfg.set_no_resp(1).set_no_cmd(1).set_resp_num(0).set_cmd_idx(0);
    if (i == 3) {
      expected_desc_cfg.set_end_of_chain(1);
    }
    EXPECT_EQ(descs[i].cmd_info, expected_desc_cfg.reg_value());
    EXPECT_EQ(descs[i].cmd_arg, 0);
    EXPECT_EQ(descs[i].data_addr, PAGE_SIZE + (i * 100));
    EXPECT_EQ(descs[i].resp_addr, 0);
  }
}

TEST_F(AmlSdmmcTest, UnownedVmoOffsetNotAligned) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
  InitializeContiguousPaddrs(1);

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 3,
      .size = 64,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
}

TEST_F(AmlSdmmcTest, UnownedVmoSingleBufferMultipleDescriptors) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  const size_t pages = ((32 * 514) / PAGE_SIZE) + 1;
  ASSERT_OK(zx::vmo::create(pages * PAGE_SIZE, 0, &vmo));
  InitializeSingleVmoPaddrs(pages);

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 16,
      .size = 32 * 513,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(511)
                               .set_block_mode(1)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, PAGE_SIZE + 16);
  EXPECT_EQ(descs[0].resp_addr, 0);

  expected_desc_cfg.set_len(2)
      .set_end_of_chain(1)
      .set_no_resp(1)
      .set_no_cmd(1)
      .set_resp_num(0)
      .set_cmd_idx(0);

  EXPECT_EQ(descs[1].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[1].cmd_arg, 0);
  EXPECT_EQ(descs[1].data_addr, PAGE_SIZE + (511 * 32) + 16);
  EXPECT_EQ(descs[1].resp_addr, 0);
}

TEST_F(AmlSdmmcTest, UnownedVmoSingleBufferNotPageAligned) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  const size_t pages = ((32 * 514) / PAGE_SIZE) + 1;
  ASSERT_OK(zx::vmo::create(pages * PAGE_SIZE, 0, &vmo));
  InitializeNonContiguousPaddrs(pages);

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 16,
      .size = 32 * 513,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
}

TEST_F(AmlSdmmcTest, UnownedVmoSingleBufferPageAligned) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  const size_t pages = ((32 * 514) / PAGE_SIZE) + 1;
  ASSERT_OK(zx::vmo::create(pages * PAGE_SIZE, 0, &vmo));
  InitializeNonContiguousPaddrs(pages);

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 32,
      .size = 32 * 513,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(127)
                               .set_block_mode(1)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, (PAGE_SIZE * 2) + 32);
  EXPECT_EQ(descs[0].resp_addr, 0);

  for (uint32_t i = 1; i < 5; i++) {
    expected_desc_cfg.set_len(128).set_no_resp(1).set_no_cmd(1).set_resp_num(0).set_cmd_idx(0);
    if (i == 4) {
      expected_desc_cfg.set_len(2).set_end_of_chain(1);
    }

    EXPECT_EQ(descs[i].cmd_info, expected_desc_cfg.reg_value());
    EXPECT_EQ(descs[i].cmd_arg, 0);
    EXPECT_EQ(descs[i].data_addr, PAGE_SIZE * (i + 1) * 2);
    EXPECT_EQ(descs[i].resp_addr, 0);
  }
}

TEST_F(AmlSdmmcTest, OwnedVmosBlockMode) {
  ASSERT_OK(dut_->Init());

  InitializeContiguousPaddrs(10);

  sdmmc_buffer_region_t buffers[10];
  for (uint32_t i = 0; i < countof(buffers); i++) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    EXPECT_OK(dut_->SdmmcRegisterVmo(i, 0, std::move(vmo), i * 64, 512, SDMMC_VMO_RIGHT_WRITE));
    buffers[i] = {
        .buffer =
            {
                .vmo_id = i,
            },
        .type = SDMMC_BUFFER_TYPE_VMO_ID,
        .offset = i * 16,
        .size = 32 * (i + 2),
    };
  }

  zx::vmo vmo;
  EXPECT_NOT_OK(dut_->SdmmcUnregisterVmo(3, 1, &vmo));

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = buffers,
      .buffers_count = countof(buffers),
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(2)
                               .set_block_mode(1)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, PAGE_SIZE);
  EXPECT_EQ(descs[0].resp_addr, 0);

  for (uint32_t i = 1; i < countof(buffers); i++) {
    expected_desc_cfg.set_len(i + 2).set_no_resp(1).set_no_cmd(1).set_resp_num(0).set_cmd_idx(0);
    if (i == countof(buffers) - 1) {
      expected_desc_cfg.set_end_of_chain(1);
    }
    EXPECT_EQ(descs[i].cmd_info, expected_desc_cfg.reg_value());
    EXPECT_EQ(descs[i].cmd_arg, 0);
    EXPECT_EQ(descs[i].data_addr, (i << 24) | (PAGE_SIZE + (i * 80)));
    EXPECT_EQ(descs[i].resp_addr, 0);
  }

  request.client_id = 7;
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_OK(dut_->SdmmcUnregisterVmo(3, 0, &vmo));
  EXPECT_NOT_OK(dut_->SdmmcRegisterVmo(2, 0, std::move(vmo), 0, 512, SDMMC_VMO_RIGHT_WRITE));

  request.client_id = 0;
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
}

TEST_F(AmlSdmmcTest, OwnedVmosNotBlockSizeMultiple) {
  ASSERT_OK(dut_->Init());

  InitializeContiguousPaddrs(10);

  sdmmc_buffer_region_t buffers[10];
  for (uint32_t i = 0; i < countof(buffers); i++) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    EXPECT_OK(dut_->SdmmcRegisterVmo(i, 0, std::move(vmo), i * 64, 512, SDMMC_VMO_RIGHT_WRITE));
    buffers[i] = {
        .buffer =
            {
                .vmo_id = i,
            },
        .type = SDMMC_BUFFER_TYPE_VMO_ID,
        .offset = 0,
        .size = 32 * (i + 2),
    };
  }

  buffers[5].size = 25;

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = buffers,
      .buffers_count = countof(buffers),
  };
  uint32_t response[4] = {};
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
}

TEST_F(AmlSdmmcTest, OwnedVmosByteMode) {
  ASSERT_OK(dut_->Init());

  InitializeContiguousPaddrs(10);

  sdmmc_buffer_region_t buffers[10];
  for (uint32_t i = 0; i < countof(buffers); i++) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    EXPECT_OK(dut_->SdmmcRegisterVmo(i, 0, std::move(vmo), i * 64, 512, SDMMC_VMO_RIGHT_WRITE));
    buffers[i] = {
        .buffer =
            {
                .vmo_id = i,
            },
        .type = SDMMC_BUFFER_TYPE_VMO_ID,
        .offset = i * 4,
        .size = 50,
    };
  }

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 50,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = buffers,
      .buffers_count = countof(buffers),
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(50)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, PAGE_SIZE);
  EXPECT_EQ(descs[0].resp_addr, 0);

  for (uint32_t i = 1; i < countof(buffers); i++) {
    expected_desc_cfg.set_len(50).set_no_resp(1).set_no_cmd(1).set_resp_num(0).set_cmd_idx(0);
    if (i == countof(buffers) - 1) {
      expected_desc_cfg.set_end_of_chain(1);
    }
    EXPECT_EQ(descs[i].cmd_info, expected_desc_cfg.reg_value());
    EXPECT_EQ(descs[i].cmd_arg, 0);
    EXPECT_EQ(descs[i].data_addr, (i << 24) | (PAGE_SIZE + (i * 68)));
    EXPECT_EQ(descs[i].resp_addr, 0);
  }
}

TEST_F(AmlSdmmcTest, OwnedVmoByteModeMultiBlock) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
  InitializeContiguousPaddrs(1);
  EXPECT_OK(dut_->SdmmcRegisterVmo(1, 0, std::move(vmo), 0, 512, SDMMC_VMO_RIGHT_WRITE));

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo_id = 1,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = 0,
      .size = 400,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 100,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(100)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, PAGE_SIZE);
  EXPECT_EQ(descs[0].resp_addr, 0);

  for (uint32_t i = 1; i < 4; i++) {
    expected_desc_cfg.set_no_resp(1).set_no_cmd(1).set_resp_num(0).set_cmd_idx(0);
    if (i == 3) {
      expected_desc_cfg.set_end_of_chain(1);
    }
    EXPECT_EQ(descs[i].cmd_info, expected_desc_cfg.reg_value());
    EXPECT_EQ(descs[i].cmd_arg, 0);
    EXPECT_EQ(descs[i].data_addr, PAGE_SIZE + (i * 100));
    EXPECT_EQ(descs[i].resp_addr, 0);
  }
}

TEST_F(AmlSdmmcTest, OwnedVmoOffsetNotAligned) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
  InitializeContiguousPaddrs(1);
  EXPECT_OK(dut_->SdmmcRegisterVmo(1, 0, std::move(vmo), 2, 512, SDMMC_VMO_RIGHT_WRITE));

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo_id = 1,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = 32,
      .size = 64,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
}

TEST_F(AmlSdmmcTest, OwnedVmoSingleBufferMultipleDescriptors) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  const size_t pages = ((32 * 514) / PAGE_SIZE) + 1;
  ASSERT_OK(zx::vmo::create(pages * PAGE_SIZE, 0, &vmo));
  InitializeSingleVmoPaddrs(pages);
  EXPECT_OK(dut_->SdmmcRegisterVmo(1, 0, std::move(vmo), 8, (pages * PAGE_SIZE) - 8,
                                   SDMMC_VMO_RIGHT_WRITE));

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo_id = 1,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = 8,
      .size = 32 * 513,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(511)
                               .set_block_mode(1)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, PAGE_SIZE + 16);
  EXPECT_EQ(descs[0].resp_addr, 0);

  expected_desc_cfg.set_len(1)
      .set_len(2)
      .set_end_of_chain(1)
      .set_no_resp(1)
      .set_no_cmd(1)
      .set_resp_num(0)
      .set_cmd_idx(0);

  EXPECT_EQ(descs[1].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[1].cmd_arg, 0);
  EXPECT_EQ(descs[1].data_addr, PAGE_SIZE + (511 * 32) + 16);
  EXPECT_EQ(descs[1].resp_addr, 0);
}

TEST_F(AmlSdmmcTest, OwnedVmoSingleBufferNotPageAligned) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  const size_t pages = ((32 * 514) / PAGE_SIZE) + 1;
  ASSERT_OK(zx::vmo::create(pages * PAGE_SIZE, 0, &vmo));
  InitializeNonContiguousPaddrs(pages);
  EXPECT_OK(dut_->SdmmcRegisterVmo(1, 0, std::move(vmo), 8, (pages * PAGE_SIZE) - 8,
                                   SDMMC_VMO_RIGHT_WRITE));

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = 1,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = 8,
      .size = 32 * 513,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
}

TEST_F(AmlSdmmcTest, OwnedVmoSingleBufferPageAligned) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  const size_t pages = ((32 * 514) / PAGE_SIZE) + 1;
  ASSERT_OK(zx::vmo::create(pages * PAGE_SIZE, 0, &vmo));
  InitializeNonContiguousPaddrs(pages);
  EXPECT_OK(dut_->SdmmcRegisterVmo(1, 0, std::move(vmo), 16, (pages * PAGE_SIZE) - 16,
                                   SDMMC_VMO_RIGHT_WRITE));

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = 1,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = 16,
      .size = 32 * 513,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(127)
                               .set_block_mode(1)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, (PAGE_SIZE * 2) + 32);
  EXPECT_EQ(descs[0].resp_addr, 0);

  for (uint32_t i = 1; i < 5; i++) {
    expected_desc_cfg.set_len(128).set_no_resp(1).set_no_cmd(1).set_resp_num(0).set_cmd_idx(0);
    if (i == 4) {
      expected_desc_cfg.set_len(2).set_end_of_chain(1);
    }

    EXPECT_EQ(descs[i].cmd_info, expected_desc_cfg.reg_value());
    EXPECT_EQ(descs[i].cmd_arg, 0);
    EXPECT_EQ(descs[i].data_addr, PAGE_SIZE * (i + 1) * 2);
    EXPECT_EQ(descs[i].resp_addr, 0);
  }
}

TEST_F(AmlSdmmcTest, OwnedVmoWritePastEnd) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  const size_t pages = ((32 * 514) / PAGE_SIZE) + 1;
  ASSERT_OK(zx::vmo::create(pages * PAGE_SIZE, 0, &vmo));
  InitializeNonContiguousPaddrs(pages);
  EXPECT_OK(dut_->SdmmcRegisterVmo(1, 0, std::move(vmo), 32, 32 * 384, SDMMC_VMO_RIGHT_WRITE));

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = 1,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = 32,
      .size = 32 * 383,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(126)
                               .set_block_mode(1)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, (PAGE_SIZE * 2) + 64);
  EXPECT_EQ(descs[0].resp_addr, 0);

  for (uint32_t i = 1; i < 4; i++) {
    expected_desc_cfg.set_len(128).set_no_resp(1).set_no_cmd(1).set_resp_num(0).set_cmd_idx(0);
    if (i == 3) {
      expected_desc_cfg.set_len(1).set_end_of_chain(1);
    }

    EXPECT_EQ(descs[i].cmd_info, expected_desc_cfg.reg_value());
    EXPECT_EQ(descs[i].cmd_arg, 0);
    EXPECT_EQ(descs[i].data_addr, PAGE_SIZE * (i + 1) * 2);
    EXPECT_EQ(descs[i].resp_addr, 0);
  }

  buffer.size = 32 * 384;
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
}

TEST_F(AmlSdmmcTest, SeparateClientVmoSpaces) {
  ASSERT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
  const zx_koid_t vmo1_koid = GetVmoKoid(vmo);
  EXPECT_NE(vmo1_koid, ZX_KOID_INVALID);
  EXPECT_OK(dut_->SdmmcRegisterVmo(1, 0, std::move(vmo), 0, PAGE_SIZE, SDMMC_VMO_RIGHT_WRITE));

  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
  const zx_koid_t vmo2_koid = GetVmoKoid(vmo);
  EXPECT_NE(vmo2_koid, ZX_KOID_INVALID);
  EXPECT_OK(dut_->SdmmcRegisterVmo(2, 0, std::move(vmo), 0, PAGE_SIZE, SDMMC_VMO_RIGHT_WRITE));

  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
  EXPECT_NOT_OK(dut_->SdmmcRegisterVmo(1, 0, std::move(vmo), 0, PAGE_SIZE, SDMMC_VMO_RIGHT_WRITE));

  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
  EXPECT_NOT_OK(dut_->SdmmcRegisterVmo(1, 8, std::move(vmo), 0, PAGE_SIZE, SDMMC_VMO_RIGHT_WRITE));

  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
  const zx_koid_t vmo3_koid = GetVmoKoid(vmo);
  EXPECT_NE(vmo3_koid, ZX_KOID_INVALID);
  EXPECT_OK(dut_->SdmmcRegisterVmo(1, 1, std::move(vmo), 0, PAGE_SIZE, SDMMC_VMO_RIGHT_WRITE));

  EXPECT_OK(dut_->SdmmcUnregisterVmo(1, 0, &vmo));
  EXPECT_EQ(GetVmoKoid(vmo), vmo1_koid);

  EXPECT_OK(dut_->SdmmcUnregisterVmo(2, 0, &vmo));
  EXPECT_EQ(GetVmoKoid(vmo), vmo2_koid);

  EXPECT_OK(dut_->SdmmcUnregisterVmo(1, 1, &vmo));
  EXPECT_EQ(GetVmoKoid(vmo), vmo3_koid);

  EXPECT_NOT_OK(dut_->SdmmcUnregisterVmo(1, 0, &vmo));
  EXPECT_NOT_OK(dut_->SdmmcUnregisterVmo(2, 0, &vmo));
  EXPECT_NOT_OK(dut_->SdmmcUnregisterVmo(1, 1, &vmo));
}

TEST_F(AmlSdmmcTest, RequestWithOwnedAndUnownedVmos) {
  ASSERT_OK(dut_->Init());

  InitializeContiguousPaddrs(10);

  zx::vmo vmos[5] = {};
  sdmmc_buffer_region_t buffers[10];
  for (uint32_t i = 0; i < 5; i++) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmos[i]));

    EXPECT_OK(dut_->SdmmcRegisterVmo(i, 0, std::move(vmo), i * 64, 512, SDMMC_VMO_RIGHT_WRITE));
    buffers[i * 2] = {
        .buffer =
            {
                .vmo_id = i,
            },
        .type = SDMMC_BUFFER_TYPE_VMO_ID,
        .offset = i * 16,
        .size = 32 * (i + 2),
    };
    buffers[(i * 2) + 1] = {
        .buffer =
            {
                .vmo = vmos[i].get(),
            },
        .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
        .offset = i * 16,
        .size = 32 * (i + 2),
    };
  }

  zx::vmo vmo;
  EXPECT_NOT_OK(dut_->SdmmcUnregisterVmo(3, 1, &vmo));

  sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = buffers,
      .buffers_count = countof(buffers),
  };
  uint32_t response[4] = {};
  AmlSdmmcCmdResp::Get().FromValue(0xfedc9876).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(response[0], 0xfedc9876);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(2)
                               .set_block_mode(1)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDMMC_READ_MULTIPLE_BLOCK)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x1234abcd);
  EXPECT_EQ(descs[0].data_addr, PAGE_SIZE);
  EXPECT_EQ(descs[0].resp_addr, 0);

  expected_desc_cfg.set_no_resp(1).set_no_cmd(1).set_resp_num(0).set_cmd_idx(0);
  EXPECT_EQ(descs[1].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[1].cmd_arg, 0);
  EXPECT_EQ(descs[1].data_addr, (5 << 24) | PAGE_SIZE);
  EXPECT_EQ(descs[1].resp_addr, 0);

  expected_desc_cfg.set_len(3);
  EXPECT_EQ(descs[2].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[2].cmd_arg, 0);
  EXPECT_EQ(descs[2].data_addr, (1 << 24) | (PAGE_SIZE + 64 + 16));
  EXPECT_EQ(descs[2].resp_addr, 0);

  EXPECT_EQ(descs[3].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[3].cmd_arg, 0);
  EXPECT_EQ(descs[3].data_addr, (6 << 24) | (PAGE_SIZE + 16));
  EXPECT_EQ(descs[3].resp_addr, 0);

  expected_desc_cfg.set_len(4);
  EXPECT_EQ(descs[4].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[4].cmd_arg, 0);
  EXPECT_EQ(descs[4].data_addr, (2 << 24) | (PAGE_SIZE + 128 + 32));
  EXPECT_EQ(descs[4].resp_addr, 0);

  EXPECT_EQ(descs[5].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[5].cmd_arg, 0);
  EXPECT_EQ(descs[5].data_addr, (7 << 24) | (PAGE_SIZE + 32));
  EXPECT_EQ(descs[5].resp_addr, 0);

  expected_desc_cfg.set_len(5);
  EXPECT_EQ(descs[6].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[6].cmd_arg, 0);
  EXPECT_EQ(descs[6].data_addr, (3 << 24) | (PAGE_SIZE + 192 + 48));
  EXPECT_EQ(descs[6].resp_addr, 0);

  EXPECT_EQ(descs[7].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[7].cmd_arg, 0);
  EXPECT_EQ(descs[7].data_addr, (8 << 24) | (PAGE_SIZE + 48));
  EXPECT_EQ(descs[7].resp_addr, 0);

  expected_desc_cfg.set_len(6);
  EXPECT_EQ(descs[8].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[8].cmd_arg, 0);
  EXPECT_EQ(descs[8].data_addr, (4 << 24) | (PAGE_SIZE + 256 + 64));
  EXPECT_EQ(descs[8].resp_addr, 0);

  expected_desc_cfg.set_end_of_chain(1);
  EXPECT_EQ(descs[9].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[9].cmd_arg, 0);
  EXPECT_EQ(descs[9].data_addr, (9 << 24) | (PAGE_SIZE + 64));
  EXPECT_EQ(descs[9].resp_addr, 0);
}

TEST_F(AmlSdmmcTest, ResetCmdInfoBits) {
  ASSERT_OK(dut_->Init());

  bti_paddrs_[1] = 0x1897'7000;
  bti_paddrs_[2] = 0x1997'8000;
  bti_paddrs_[3] = 0x1997'e000;

  // Make sure the appropriate cmd_info bits get cleared.
  dut_->descs()[0].cmd_info = 0xffff'ffff;
  dut_->descs()[1].cmd_info = 0xffff'ffff;
  dut_->descs()[2].cmd_info = 0xffff'ffff;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE * 3, 0, &vmo));
  EXPECT_OK(dut_->SdmmcRegisterVmo(1, 2, std::move(vmo), 0, PAGE_SIZE * 3, SDMMC_VMO_RIGHT_WRITE));

  sdmmc_buffer_region_t buffer = {
      .buffer = {.vmo_id = 1},
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = 0,
      .size = 10752,
  };

  sdmmc_req_new_t request = {
      .cmd_idx = SDIO_IO_RW_DIRECT_EXTENDED,
      .cmd_flags = SDIO_IO_RW_DIRECT_EXTENDED_FLAGS | SDMMC_CMD_READ,
      .arg = 0x29000015,
      .blocksize = 512,
      .probe_tuning_cmd = false,
      .client_id = 2,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_blk_len(0).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));
  EXPECT_EQ(AmlSdmmcCfg::Get().ReadFrom(&mmio_).blk_len(), 9);

  const aml_sdmmc_desc_t* descs = dut_->descs();
  auto expected_desc_cfg = AmlSdmmcCmdCfg::Get()
                               .FromValue(0)
                               .set_len(8)
                               .set_block_mode(1)
                               .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
                               .set_data_io(1)
                               .set_data_wr(0)
                               .set_resp_num(1)
                               .set_cmd_idx(SDIO_IO_RW_DIRECT_EXTENDED)
                               .set_owner(1);

  EXPECT_EQ(descs[0].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[0].cmd_arg, 0x29000015);
  EXPECT_EQ(descs[0].data_addr, 0x1897'7000);
  EXPECT_EQ(descs[0].resp_addr, 0);

  expected_desc_cfg.set_no_resp(1).set_no_cmd(1).set_resp_num(0).set_cmd_idx(0);
  EXPECT_EQ(descs[1].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[1].cmd_arg, 0);
  EXPECT_EQ(descs[1].data_addr, 0x1997'8000);
  EXPECT_EQ(descs[1].resp_addr, 0);

  expected_desc_cfg.set_len(5).set_end_of_chain(1);
  EXPECT_EQ(descs[2].cmd_info, expected_desc_cfg.reg_value());
  EXPECT_EQ(descs[2].cmd_arg, 0);
  EXPECT_EQ(descs[2].data_addr, 0x1997'e000);
  EXPECT_EQ(descs[2].resp_addr, 0);
}

TEST_F(AmlSdmmcTest, WriteToReadOnlyVmo) {
  ASSERT_OK(dut_->Init());

  InitializeContiguousPaddrs(10);

  sdmmc_buffer_region_t buffers[10];
  for (uint32_t i = 0; i < countof(buffers); i++) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    const uint32_t vmo_rights = SDMMC_VMO_RIGHT_READ | (i == 5 ? 0 : SDMMC_VMO_RIGHT_WRITE);
    EXPECT_OK(dut_->SdmmcRegisterVmo(i, 0, std::move(vmo), i * 64, 512, vmo_rights));
    buffers[i] = {
        .buffer =
            {
                .vmo_id = i,
            },
        .type = SDMMC_BUFFER_TYPE_VMO_ID,
        .offset = 0,
        .size = 32 * (i + 2),
    };
  }

  sdmmc_req_new_t request = {
      .cmd_idx = SDIO_IO_RW_DIRECT_EXTENDED,
      .cmd_flags = SDIO_IO_RW_DIRECT_EXTENDED_FLAGS | SDMMC_CMD_READ,
      .arg = 0x29000015,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = buffers,
      .buffers_count = countof(buffers),
  };
  uint32_t response[4] = {};
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
}

TEST_F(AmlSdmmcTest, ReadFromWriteOnlyVmo) {
  ASSERT_OK(dut_->Init());

  InitializeContiguousPaddrs(10);

  sdmmc_buffer_region_t buffers[10];
  for (uint32_t i = 0; i < countof(buffers); i++) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    const uint32_t vmo_rights = SDMMC_VMO_RIGHT_WRITE | (i == 5 ? 0 : SDMMC_VMO_RIGHT_READ);
    EXPECT_OK(dut_->SdmmcRegisterVmo(i, 0, std::move(vmo), i * 64, 512, vmo_rights));
    buffers[i] = {
        .buffer =
            {
                .vmo_id = i,
            },
        .type = SDMMC_BUFFER_TYPE_VMO_ID,
        .offset = 0,
        .size = 32 * (i + 2),
    };
  }

  sdmmc_req_new_t request = {
      .cmd_idx = SDIO_IO_RW_DIRECT_EXTENDED,
      .cmd_flags = SDIO_IO_RW_DIRECT_EXTENDED_FLAGS,
      .arg = 0x29000015,
      .blocksize = 32,
      .probe_tuning_cmd = false,
      .client_id = 0,
      .buffers_list = buffers,
      .buffers_count = countof(buffers),
  };
  uint32_t response[4] = {};
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
}

}  // namespace sdmmc
