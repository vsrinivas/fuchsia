// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdhci.h"

#include <atomic>
#include <memory>
#include <optional>

#include <lib/fake_ddk/fake_ddk.h>
#include <mock/ddktl/protocol/sdhci.h>
#include <zxtest/zxtest.h>

namespace sdhci {

class TestSdhci : public Sdhci {
 public:
  TestSdhci(zx_device_t* parent, ddk::MmioBuffer regs_mmio_buffer,
            const ddk::SdhciProtocolClient sdhci)
      : Sdhci(parent, std::move(regs_mmio_buffer), {}, {}, sdhci) {}

  zx_status_t SdmmcRequest(sdmmc_req_t* req) {
    blocks_remaining_ = req->blockcount;
    current_block_ = 0;
    return Sdhci::SdmmcRequest(req);
  }

  void DdkUnbindNew(ddk::UnbindTxn txn) {
    run_thread_ = false;
    Sdhci::DdkUnbindNew(std::move(txn));
  }

 protected:
  zx_status_t WaitForReset(const uint32_t mask, zx::duration timeout) const override {
    return ZX_OK;
  }

  zx_status_t WaitForInterrupt() override {
    regs_mmio_buffer_.Write<uint16_t>(0x0000, 0x30);
    regs_mmio_buffer_.Write<uint16_t>(0x0000, 0x32);

    while (run_thread_) {
      switch (GetRequestStatus()) {
        case RequestStatus::COMMAND:
          regs_mmio_buffer_.Write<uint16_t>(0x0001, 0x30);
          return ZX_OK;
          break;
        case RequestStatus::TRANSFER_DATA_DMA:
          regs_mmio_buffer_.Write<uint16_t>(0x0002, 0x30);
          return ZX_OK;
          break;
        case RequestStatus::READ_DATA_PIO:
          if (++current_block_ == blocks_remaining_) {
            regs_mmio_buffer_.Write<uint16_t>(0x0022, 0x30);
          } else {
            regs_mmio_buffer_.Write<uint16_t>(0x0020, 0x30);
          }
          return ZX_OK;
          break;
        case RequestStatus::WRITE_DATA_PIO:
          if (++current_block_ == blocks_remaining_) {
            regs_mmio_buffer_.Write<uint16_t>(0x0012, 0x30);
          } else {
            regs_mmio_buffer_.Write<uint16_t>(0x0010, 0x30);
          }
          return ZX_OK;
          break;
        default:
          break;
      }
    }

    return ZX_ERR_CANCELED;
  }

 private:
  std::atomic<bool> run_thread_ = true;
  std::atomic<uint16_t> blocks_remaining_ = 0;
  std::atomic<uint16_t> current_block_ = 0;
};

class SdhciTest : public zxtest::Test {
 public:
  SdhciTest()
      : registers_(new uint8_t[kMmioSize]),
        mmio_(
            {
                .vaddr = registers_.get(),
                .offset = 0,
                .size = kMmioSize,
                .vmo = ZX_HANDLE_INVALID,
            },
            0) {}

  void SetUp() override { ASSERT_TRUE(registers_); }

 protected:
  void CreateDut() {
    memset(registers_.get(), 0, kMmioSize);

    dut_.emplace(fake_ddk::kFakeParent, ddk::MmioView(mmio_),
                 ddk::SdhciProtocolClient(mock_sdhci_.GetProto()));

    mmio_.Write<uint16_t>(0x0002, 0xfe);
    mmio_.Write<uint16_t>(0x0002, 0x2c);
  }

  std::unique_ptr<uint8_t[]> registers_;
  ddk::MockSdhci mock_sdhci_;
  zx::interrupt irq_;
  std::optional<TestSdhci> dut_;
  ddk::MmioView mmio_;

 private:
  static constexpr size_t kMmioSize = 0x200;
};

TEST_F(SdhciTest, DdkLifecycle) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  EXPECT_OK(dut_->Init());

  fake_ddk::Bind bind;
  dut_->DdkAdd("sdhci");
  dut_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  EXPECT_TRUE(bind.Ok());
}

TEST_F(SdhciTest, BaseClockZero) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(0);
  EXPECT_NOT_OK(dut_->Init());
}

TEST_F(SdhciTest, BaseClockFromDriver) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(0xabcdef);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  EXPECT_EQ(dut_->base_clock(), 0xabcdef);
}

TEST_F(SdhciTest, BaseClockFromHardware) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mmio_.Write<uint64_t>(0x0000'0000'0000'6800, 0x40);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  EXPECT_EQ(dut_->base_clock(), 104'000'000);
}

TEST_F(SdhciTest, HostInfo) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mmio_.Write<uint64_t>(0x0000'0000'1104'0100, 0x40);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  sdmmc_host_info_t host_info = {};
  EXPECT_OK(dut_->SdmmcHostInfo(&host_info));
  EXPECT_EQ(host_info.caps, SDMMC_HOST_CAP_BUS_WIDTH_8 | SDMMC_HOST_CAP_SIXTY_FOUR_BIT |
                                SDMMC_HOST_CAP_VOLTAGE_330 | SDMMC_HOST_CAP_AUTO_CMD12);
  EXPECT_EQ(host_info.prefs, 0);
}

TEST_F(SdhciTest, HostInfoNoDma) {
  mock_sdhci_.ExpectGetQuirks(SDHCI_QUIRK_NO_DMA);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mmio_.Write<uint64_t>(0x0000'0000'1104'0100, 0x40);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  sdmmc_host_info_t host_info = {};
  EXPECT_OK(dut_->SdmmcHostInfo(&host_info));
  EXPECT_EQ(host_info.caps,
            SDMMC_HOST_CAP_BUS_WIDTH_8 | SDMMC_HOST_CAP_VOLTAGE_330 | SDMMC_HOST_CAP_AUTO_CMD12);
  EXPECT_EQ(host_info.prefs, 0);
}

TEST_F(SdhciTest, HostInfoNoTuning) {
  mock_sdhci_.ExpectGetQuirks(SDHCI_QUIRK_NON_STANDARD_TUNING);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mmio_.Write<uint64_t>(0x0000'0000'0000'0100, 0x40);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  sdmmc_host_info_t host_info = {};
  EXPECT_OK(dut_->SdmmcHostInfo(&host_info));
  EXPECT_EQ(host_info.caps, SDMMC_HOST_CAP_AUTO_CMD12);
  EXPECT_EQ(host_info.prefs, SDMMC_HOST_PREFS_DISABLE_HS400 | SDMMC_HOST_PREFS_DISABLE_HS200);
}

TEST_F(SdhciTest, SetSignalVoltage) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  mmio_.Write<uint64_t>((1 << 26) | (1 << 24), 0x40);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  mmio_.Write<uint8_t>(0b0000'1011, 0x29);
  EXPECT_OK(dut_->SdmmcSetSignalVoltage(SDMMC_VOLTAGE_V180));
  EXPECT_TRUE(mmio_.Read<uint16_t>(0x3e) & (1 << 3));

  mmio_.Write<uint8_t>(0b0000'1111, 0x29);
  EXPECT_OK(dut_->SdmmcSetSignalVoltage(SDMMC_VOLTAGE_V330));
  EXPECT_FALSE(mmio_.Read<uint16_t>(0x3e) & (1 << 3));
}

TEST_F(SdhciTest, SetSignalVoltageUnsupported) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  EXPECT_NOT_OK(dut_->SdmmcSetSignalVoltage(SDMMC_VOLTAGE_V330));
}

TEST_F(SdhciTest, SetBusWidth) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  mmio_.Write<uint64_t>(1 << 18, 0x40);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  EXPECT_OK(dut_->SdmmcSetBusWidth(SDMMC_BUS_WIDTH_EIGHT));
  EXPECT_EQ(mmio_.Read<uint8_t>(0x28) & 0b0010'0000, 0b0010'0000);

  EXPECT_OK(dut_->SdmmcSetBusWidth(SDMMC_BUS_WIDTH_ONE));
  EXPECT_EQ(mmio_.Read<uint8_t>(0x28), 0);

  EXPECT_OK(dut_->SdmmcSetBusWidth(SDMMC_BUS_WIDTH_FOUR));
  EXPECT_EQ(mmio_.Read<uint8_t>(0x28) & 0b0000'0010, 0b0000'0010);
}

TEST_F(SdhciTest, SetBusWidthNotSupported) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  EXPECT_NOT_OK(dut_->SdmmcSetBusWidth(SDMMC_BUS_WIDTH_EIGHT));
}

TEST_F(SdhciTest, SetBusFreq) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  EXPECT_OK(dut_->SdmmcSetBusFreq(12'500'000));
  EXPECT_EQ(mmio_.Read<uint16_t>(0x2c) & 0b1111'1111'1100'0100, 0b0000'0100'0000'0100);

  EXPECT_OK(dut_->SdmmcSetBusFreq(65'190));
  EXPECT_EQ(mmio_.Read<uint16_t>(0x2c) & 0b1111'1111'1100'0100, 0b1111'1111'1000'0100);

  EXPECT_OK(dut_->SdmmcSetBusFreq(100'000'000));
  EXPECT_EQ(mmio_.Read<uint16_t>(0x2c) & 0b1111'1111'1100'0100, 0b0000'0000'0000'0100);

  EXPECT_OK(dut_->SdmmcSetBusFreq(26'000'000));
  EXPECT_EQ(mmio_.Read<uint16_t>(0x2c) & 0b1111'1111'1100'0100, 0b0000'0010'0000'0100);
}

TEST_F(SdhciTest, HwReset) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mock_sdhci_.ExpectHwReset();
  dut_->SdmmcHwReset();
  ASSERT_NO_FATAL_FAILURES(mock_sdhci_.VerifyAndClear());
}

TEST_F(SdhciTest, RequestCommandOnly) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  EXPECT_OK(dut_->Init());

  sdmmc_req_t request = {
      .cmd_idx = SDMMC_SEND_STATUS,
      .cmd_flags = SDMMC_SEND_STATUS_FLAGS,
      .arg = 0x7b7d9fbd,
      .blockcount = 0,
      .blocksize = 0,
      .use_dma = false,
      .dma_vmo = ZX_HANDLE_INVALID,
      .virt_buffer = nullptr,
      .virt_size = 0,
      .buf_offset = 0,
      .pmt = ZX_HANDLE_INVALID,
      .probe_tuning_cmd = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };

  mmio_.Write<uint32_t>(0xf3bbf2c0, 0x10);
  EXPECT_OK(dut_->SdmmcRequest(&request));

  EXPECT_EQ(mmio_.Read<uint32_t>(0x08), 0x7b7d9fbd);
  EXPECT_EQ(mmio_.Read<uint16_t>(0x0e), 0x0d1a);

  EXPECT_OK(request.status);
  EXPECT_EQ(request.response[0], 0xf3bbf2c0);

  request = {
      .cmd_idx = SDMMC_SEND_CSD,
      .cmd_flags = SDMMC_SEND_CSD_FLAGS,
      .arg = 0x9c1dc1ed,
      .blockcount = 0,
      .blocksize = 0,
      .use_dma = false,
      .dma_vmo = ZX_HANDLE_INVALID,
      .virt_buffer = nullptr,
      .virt_size = 0,
      .buf_offset = 0,
      .pmt = ZX_HANDLE_INVALID,
      .probe_tuning_cmd = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };

  mmio_.Write<uint32_t>(0x9f93b17d, 0x10);
  mmio_.Write<uint32_t>(0x89aaba9e, 0x14);
  mmio_.Write<uint32_t>(0xc14b059e, 0x18);
  mmio_.Write<uint32_t>(0x7329a9e3, 0x1c);
  EXPECT_OK(dut_->SdmmcRequest(&request));

  EXPECT_EQ(mmio_.Read<uint32_t>(0x08), 0x9c1dc1ed);
  EXPECT_EQ(mmio_.Read<uint16_t>(0x0e), 0x0909);

  EXPECT_OK(request.status);
  EXPECT_EQ(request.response[0], 0x9f93b17d);
  EXPECT_EQ(request.response[1], 0x89aaba9e);
  EXPECT_EQ(request.response[2], 0xc14b059e);
  EXPECT_EQ(request.response[3], 0x7329a9e3);

  dut_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, RequestWithData) {
  mock_sdhci_.ExpectGetQuirks(0);
  ASSERT_NO_FATAL_FAILURES(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  EXPECT_OK(dut_->Init());

  uint32_t buffer[16] = {
      // clang-format off
      0x178096fb, 0x27328a47, 0x3267ce33, 0x8fccdf57,
      0x84d24349, 0x68fd8e47, 0x6b7363a3, 0x5f9fb9b1,
      0xfa0263f0, 0x467731aa, 0xf1a95135, 0xe9e7ba6b,
      0x2112719a, 0x7ee23bad, 0xb4285417, 0x6db4a2d1,
      // clang-format on
  };

  sdmmc_req_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0xfc4e6f56,
      .blockcount = 4,
      .blocksize = 16,
      .use_dma = false,
      .dma_vmo = ZX_HANDLE_INVALID,
      .virt_buffer = buffer,
      .virt_size = 0,
      .buf_offset = 0,
      .pmt = ZX_HANDLE_INVALID,
      .probe_tuning_cmd = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };

  mmio_.Write<uint32_t>(0x4ea3f1f3, 0x10);
  EXPECT_OK(dut_->SdmmcRequest(&request));

  EXPECT_EQ(mmio_.Read<uint16_t>(0x04), 16);
  EXPECT_EQ(mmio_.Read<uint16_t>(0x06), 4);
  EXPECT_EQ(mmio_.Read<uint32_t>(0x08), 0xfc4e6f56);
  EXPECT_EQ(mmio_.Read<uint16_t>(0x0c), 0x0026);
  EXPECT_EQ(mmio_.Read<uint16_t>(0x0e), 0x193a);
  EXPECT_EQ(mmio_.Read<uint32_t>(0x20), 0x6db4a2d1);

  EXPECT_OK(request.status);
  EXPECT_EQ(request.response[0], 0x4ea3f1f3);

  request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x55c1c22c,
      .blockcount = 4,
      .blocksize = 16,
      .use_dma = false,
      .dma_vmo = ZX_HANDLE_INVALID,
      .virt_buffer = buffer,
      .virt_size = 0,
      .buf_offset = 0,
      .pmt = ZX_HANDLE_INVALID,
      .probe_tuning_cmd = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };

  mmio_.Write<uint32_t>(0xa5387c19, 0x10);
  mmio_.Write<uint32_t>(0xe99dd637, 0x20);
  EXPECT_OK(dut_->SdmmcRequest(&request));

  EXPECT_EQ(mmio_.Read<uint16_t>(0x04), 16);
  EXPECT_EQ(mmio_.Read<uint16_t>(0x06), 4);
  EXPECT_EQ(mmio_.Read<uint32_t>(0x08), 0x55c1c22c);
  EXPECT_EQ(mmio_.Read<uint16_t>(0x0c), 0x0036);
  EXPECT_EQ(mmio_.Read<uint16_t>(0x0e), 0x123a);

  EXPECT_OK(request.status);
  EXPECT_EQ(request.response[0], 0xa5387c19);

  for (uint32_t i = 0; i < 16; i++) {
    EXPECT_EQ(buffer[i], 0xe99dd637);
  }

  dut_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

}  // namespace sdhci
