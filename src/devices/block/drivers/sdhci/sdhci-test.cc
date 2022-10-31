// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdhci.h"

#include <fuchsia/hardware/sdhci/cpp/banjo-mock.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio-ptr/fake.h>
#include <lib/sync/completion.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include <zxtest/zxtest.h>

// Stub out vmo_op_range to allow tests to use fake VMOs.
__EXPORT
zx_status_t zx_vmo_op_range(zx_handle_t handle, uint32_t op, uint64_t offset, uint64_t size,
                            void* buffer, size_t buffer_size) {
  return ZX_OK;
}

namespace {

zx_paddr_t PageMask() { return static_cast<uintptr_t>(zx_system_get_page_size()) - 1; }

}  // namespace

namespace sdhci {

class TestSdhci : public Sdhci {
 public:
  TestSdhci(zx_device_t* parent, fdf::MmioBuffer regs_mmio_buffer, zx::bti bti,
            const ddk::SdhciProtocolClient sdhci, uint64_t quirks, uint64_t dma_boundary_alignment)
      : Sdhci(parent, std::move(regs_mmio_buffer), std::move(bti), {}, sdhci, quirks,
              dma_boundary_alignment) {}

  zx_status_t SdmmcRequest(sdmmc_req_t* req) {
    blocks_remaining_ = req->blockcount;
    current_block_ = 0;
    return Sdhci::SdmmcRequest(req);
  }

  zx_status_t SdmmcRequestNew(const sdmmc_req_new_t* req, uint32_t out_response[4]) {
    cpp20::span<const sdmmc_buffer_region_t> buffers(req->buffers_list, req->buffers_count);
    size_t bytes = 0;
    for (const sdmmc_buffer_region_t& buffer : buffers) {
      bytes += buffer.size;
    }
    blocks_remaining_ = req->blocksize ? static_cast<uint16_t>(bytes / req->blocksize) : 0;
    current_block_ = 0;
    return Sdhci::SdmmcRequestNew(req, out_response);
  }

  void DdkUnbind(ddk::UnbindTxn txn) {
    run_thread_ = false;
    Sdhci::DdkUnbind(std::move(txn));
  }

  uint8_t reset_mask() {
    uint8_t ret = reset_mask_;
    reset_mask_ = 0;
    return ret;
  }

  void* iobuf_virt() const { return iobuf_.virt(); }

  void TriggerCardInterrupt() { card_interrupt_ = true; }
  void InjectTransferError() { inject_error_ = true; }

 protected:
  zx_status_t WaitForReset(const SoftwareReset mask) override {
    reset_mask_ = mask.reg_value();
    return ZX_OK;
  }

  zx_status_t WaitForInterrupt() override {
    auto status = InterruptStatus::Get().FromValue(0).WriteTo(&regs_mmio_buffer_);

    while (run_thread_) {
      switch (GetRequestStatus()) {
        case RequestStatus::COMMAND:
          status.set_command_complete(1).WriteTo(&regs_mmio_buffer_);
          return ZX_OK;
        case RequestStatus::TRANSFER_DATA_DMA:
          status.set_transfer_complete(1);
          if (inject_error_) {
            status.set_error(1).set_data_crc_error(1);
          }
          status.WriteTo(&regs_mmio_buffer_);
          return ZX_OK;
        case RequestStatus::READ_DATA_PIO:
          if (++current_block_ == blocks_remaining_) {
            status.set_buffer_read_ready(1).set_transfer_complete(1).WriteTo(&regs_mmio_buffer_);
          } else {
            status.set_buffer_read_ready(1).WriteTo(&regs_mmio_buffer_);
          }
          return ZX_OK;
        case RequestStatus::WRITE_DATA_PIO:
          if (++current_block_ == blocks_remaining_) {
            status.set_buffer_write_ready(1).set_transfer_complete(1).WriteTo(&regs_mmio_buffer_);
          } else {
            status.set_buffer_write_ready(1).WriteTo(&regs_mmio_buffer_);
          }
          return ZX_OK;
        case RequestStatus::BUSY_RESPONSE:
          status.set_transfer_complete(1).WriteTo(&regs_mmio_buffer_);
          return ZX_OK;
        default:
          break;
      }

      if (card_interrupt_.exchange(false) &&
          InterruptStatusEnable::Get().ReadFrom(&regs_mmio_buffer_).card_interrupt() == 1) {
        status.set_card_interrupt(1).WriteTo(&regs_mmio_buffer_);
        return ZX_OK;
      }
    }

    return ZX_ERR_CANCELED;
  }

 private:
  uint8_t reset_mask_ = 0;
  std::atomic<bool> run_thread_ = true;
  std::atomic<uint16_t> blocks_remaining_ = 0;
  std::atomic<uint16_t> current_block_ = 0;
  std::atomic<bool> card_interrupt_ = false;
  std::atomic<bool> inject_error_ = false;
};

class SdhciTest : public zxtest::Test {
 public:
  SdhciTest()
      : registers_(new uint8_t[kRegisterSetSize]),
        mmio_(
            {
                .vaddr = FakeMmioPtr(registers_.get()),
                .offset = 0,
                .size = kRegisterSetSize,
                .vmo = ZX_HANDLE_INVALID,
            },
            0) {}

 protected:
  void CreateDut(std::vector<zx_paddr_t> dma_paddrs, uint64_t quirks = 0,
                 uint64_t dma_boundary_alignment = 0) {
    zx::bti fake_bti;
    dma_paddrs_ = std::move(dma_paddrs);
    ASSERT_OK(fake_bti_create_with_paddrs(dma_paddrs_.data(), dma_paddrs_.size(),
                                          fake_bti.reset_and_get_address()));

    memset(registers_.get(), 0, kRegisterSetSize);

    bti_ = fake_bti.borrow();
    dut_.emplace(fake_ddk::kFakeParent, fdf::MmioView(mmio_), std::move(fake_bti),
                 ddk::SdhciProtocolClient(mock_sdhci_.GetProto()), quirks, dma_boundary_alignment);

    HostControllerVersion::Get()
        .FromValue(0)
        .set_specification_version(HostControllerVersion::kSpecificationVersion300)
        .WriteTo(&mmio_);
    ClockControl::Get().FromValue(0).set_internal_clock_stable(1).WriteTo(&mmio_);
  }

  void CreateDut(uint64_t quirks = 0, uint64_t dma_boundary_alignment = 0) {
    CreateDut({}, quirks, dma_boundary_alignment);
  }

  void ExpectPmoCount(uint64_t count) {
    zx_info_bti_t bti_info;
    EXPECT_OK(bti_->get_info(ZX_INFO_BTI, &bti_info, sizeof(bti_info), nullptr, nullptr));
    EXPECT_EQ(bti_info.pmo_count, count);
  }

  std::unique_ptr<uint8_t[]> registers_;
  ddk::MockSdhci mock_sdhci_;
  zx::interrupt irq_;
  std::vector<zx_paddr_t> dma_paddrs_;
  std::optional<TestSdhci> dut_;
  fdf::MmioView mmio_;
  zx::unowned_bti bti_;
};

TEST_F(SdhciTest, DdkLifecycle) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  EXPECT_OK(dut_->Init());

  fake_ddk::Bind bind;
  dut_->DdkAdd("sdhci");
  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  EXPECT_TRUE(bind.Ok());
}

TEST_F(SdhciTest, BaseClockZero) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(0);
  EXPECT_NOT_OK(dut_->Init());
}

TEST_F(SdhciTest, BaseClockFromDriver) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(0xabcdef);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  EXPECT_EQ(dut_->base_clock(), 0xabcdef);
}

TEST_F(SdhciTest, BaseClockFromHardware) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  Capabilities0::Get().FromValue(0).set_base_clock_frequency(104).WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  EXPECT_EQ(dut_->base_clock(), 104'000'000);
}

TEST_F(SdhciTest, HostInfo) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  Capabilities1::Get()
      .FromValue(0)
      .set_sdr50_support(1)
      .set_sdr104_support(1)
      .set_use_tuning_for_sdr50(1)
      .WriteTo(&mmio_);
  Capabilities0::Get()
      .FromValue(0)
      .set_base_clock_frequency(1)
      .set_bus_width_8_support(1)
      .set_voltage_3v3_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  sdmmc_host_info_t host_info = {};
  EXPECT_OK(dut_->SdmmcHostInfo(&host_info));
  EXPECT_EQ(host_info.caps, SDMMC_HOST_CAP_BUS_WIDTH_8 | SDMMC_HOST_CAP_VOLTAGE_330 |
                                SDMMC_HOST_CAP_AUTO_CMD12 | SDMMC_HOST_CAP_SDR50 |
                                SDMMC_HOST_CAP_SDR104);
  EXPECT_EQ(host_info.prefs, 0);
}

TEST_F(SdhciTest, HostInfoNoDma) {
  ASSERT_NO_FATAL_FAILURE(CreateDut(SDHCI_QUIRK_NO_DMA));

  Capabilities1::Get().FromValue(0).set_sdr50_support(1).set_ddr50_support(1).WriteTo(&mmio_);
  Capabilities0::Get()
      .FromValue(0)
      .set_base_clock_frequency(1)
      .set_bus_width_8_support(1)
      .set_voltage_3v3_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  sdmmc_host_info_t host_info = {};
  EXPECT_OK(dut_->SdmmcHostInfo(&host_info));
  EXPECT_EQ(host_info.caps, SDMMC_HOST_CAP_BUS_WIDTH_8 | SDMMC_HOST_CAP_VOLTAGE_330 |
                                SDMMC_HOST_CAP_AUTO_CMD12 | SDMMC_HOST_CAP_DDR50 |
                                SDMMC_HOST_CAP_SDR50 | SDMMC_HOST_CAP_NO_TUNING_SDR50);
  EXPECT_EQ(host_info.prefs, 0);
}

TEST_F(SdhciTest, HostInfoNoTuning) {
  ASSERT_NO_FATAL_FAILURE(CreateDut(SDHCI_QUIRK_NON_STANDARD_TUNING));

  Capabilities1::Get().FromValue(0).WriteTo(&mmio_);
  Capabilities0::Get().FromValue(0).set_base_clock_frequency(1).WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  sdmmc_host_info_t host_info = {};
  EXPECT_OK(dut_->SdmmcHostInfo(&host_info));
  EXPECT_EQ(host_info.caps, SDMMC_HOST_CAP_AUTO_CMD12 | SDMMC_HOST_CAP_NO_TUNING_SDR50);
  EXPECT_EQ(host_info.prefs, SDMMC_HOST_PREFS_DISABLE_HS400 | SDMMC_HOST_PREFS_DISABLE_HS200);
}

TEST_F(SdhciTest, SetSignalVoltage) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get().FromValue(0).set_voltage_3v3_support(1).set_voltage_1v8_support(1).WriteTo(
      &mmio_);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  PresentState::Get().FromValue(0).set_dat_3_0(0b0001).WriteTo(&mmio_);

  PowerControl::Get()
      .FromValue(0)
      .set_sd_bus_voltage_vdd1(PowerControl::kBusVoltage1V8)
      .set_sd_bus_power_vdd1(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcSetSignalVoltage(SDMMC_VOLTAGE_V180));
  EXPECT_TRUE(HostControl2::Get().ReadFrom(&mmio_).voltage_1v8_signalling_enable());

  PowerControl::Get()
      .FromValue(0)
      .set_sd_bus_voltage_vdd1(PowerControl::kBusVoltage3V3)
      .set_sd_bus_power_vdd1(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcSetSignalVoltage(SDMMC_VOLTAGE_V330));
  EXPECT_FALSE(HostControl2::Get().ReadFrom(&mmio_).voltage_1v8_signalling_enable());
}

TEST_F(SdhciTest, SetSignalVoltageUnsupported) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  EXPECT_NOT_OK(dut_->SdmmcSetSignalVoltage(SDMMC_VOLTAGE_V330));
}

TEST_F(SdhciTest, SetBusWidth) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get().FromValue(0).set_bus_width_8_support(1).WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  auto ctrl1 = HostControl1::Get().FromValue(0);

  EXPECT_OK(dut_->SdmmcSetBusWidth(SDMMC_BUS_WIDTH_EIGHT));
  EXPECT_TRUE(ctrl1.ReadFrom(&mmio_).extended_data_transfer_width());
  EXPECT_FALSE(ctrl1.ReadFrom(&mmio_).data_transfer_width_4bit());

  EXPECT_OK(dut_->SdmmcSetBusWidth(SDMMC_BUS_WIDTH_ONE));
  EXPECT_FALSE(ctrl1.ReadFrom(&mmio_).extended_data_transfer_width());
  EXPECT_FALSE(ctrl1.ReadFrom(&mmio_).data_transfer_width_4bit());

  EXPECT_OK(dut_->SdmmcSetBusWidth(SDMMC_BUS_WIDTH_FOUR));
  EXPECT_FALSE(ctrl1.ReadFrom(&mmio_).extended_data_transfer_width());
  EXPECT_TRUE(ctrl1.ReadFrom(&mmio_).data_transfer_width_4bit());
}

TEST_F(SdhciTest, SetBusWidthNotSupported) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  EXPECT_NOT_OK(dut_->SdmmcSetBusWidth(SDMMC_BUS_WIDTH_EIGHT));
}

TEST_F(SdhciTest, SetBusFreq) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  auto clock = ClockControl::Get().FromValue(0);

  EXPECT_OK(dut_->SdmmcSetBusFreq(12'500'000));
  EXPECT_EQ(clock.ReadFrom(&mmio_).frequency_select(), 4);
  EXPECT_TRUE(clock.sd_clock_enable());

  EXPECT_OK(dut_->SdmmcSetBusFreq(65'190));
  EXPECT_EQ(clock.ReadFrom(&mmio_).frequency_select(), 767);
  EXPECT_TRUE(clock.sd_clock_enable());

  EXPECT_OK(dut_->SdmmcSetBusFreq(100'000'000));
  EXPECT_EQ(clock.ReadFrom(&mmio_).frequency_select(), 0);
  EXPECT_TRUE(clock.sd_clock_enable());

  EXPECT_OK(dut_->SdmmcSetBusFreq(26'000'000));
  EXPECT_EQ(clock.ReadFrom(&mmio_).frequency_select(), 2);
  EXPECT_TRUE(clock.sd_clock_enable());

  EXPECT_OK(dut_->SdmmcSetBusFreq(0));
  EXPECT_FALSE(clock.ReadFrom(&mmio_).sd_clock_enable());
}

TEST_F(SdhciTest, SetBusFreqTimeout) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  ClockControl::Get().FromValue(0).set_internal_clock_stable(1).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcSetBusFreq(12'500'000));

  ClockControl::Get().FromValue(0).WriteTo(&mmio_);
  EXPECT_NOT_OK(dut_->SdmmcSetBusFreq(12'500'000));
}

TEST_F(SdhciTest, SetBusFreqInternalClockEnable) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  EXPECT_OK(dut_->Init());
  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));

  ClockControl::Get()
      .FromValue(0)
      .set_internal_clock_stable(1)
      .set_internal_clock_enable(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcSetBusFreq(12'500'000));
  EXPECT_TRUE(ClockControl::Get().ReadFrom(&mmio_).internal_clock_enable());
}

TEST_F(SdhciTest, SetTiming) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  EXPECT_OK(dut_->SdmmcSetTiming(SDMMC_TIMING_HS));
  EXPECT_TRUE(HostControl1::Get().ReadFrom(&mmio_).high_speed_enable());
  EXPECT_EQ(HostControl2::Get().ReadFrom(&mmio_).uhs_mode_select(), HostControl2::kUhsModeSdr25);

  EXPECT_OK(dut_->SdmmcSetTiming(SDMMC_TIMING_LEGACY));
  EXPECT_FALSE(HostControl1::Get().ReadFrom(&mmio_).high_speed_enable());
  EXPECT_EQ(HostControl2::Get().ReadFrom(&mmio_).uhs_mode_select(), HostControl2::kUhsModeSdr12);

  EXPECT_OK(dut_->SdmmcSetTiming(SDMMC_TIMING_HSDDR));
  EXPECT_TRUE(HostControl1::Get().ReadFrom(&mmio_).high_speed_enable());
  EXPECT_EQ(HostControl2::Get().ReadFrom(&mmio_).uhs_mode_select(), HostControl2::kUhsModeDdr50);

  EXPECT_OK(dut_->SdmmcSetTiming(SDMMC_TIMING_SDR25));
  EXPECT_TRUE(HostControl1::Get().ReadFrom(&mmio_).high_speed_enable());
  EXPECT_EQ(HostControl2::Get().ReadFrom(&mmio_).uhs_mode_select(), HostControl2::kUhsModeSdr25);

  EXPECT_OK(dut_->SdmmcSetTiming(SDMMC_TIMING_SDR12));
  EXPECT_TRUE(HostControl1::Get().ReadFrom(&mmio_).high_speed_enable());
  EXPECT_EQ(HostControl2::Get().ReadFrom(&mmio_).uhs_mode_select(), HostControl2::kUhsModeSdr12);

  EXPECT_OK(dut_->SdmmcSetTiming(SDMMC_TIMING_HS400));
  EXPECT_TRUE(HostControl1::Get().ReadFrom(&mmio_).high_speed_enable());
  EXPECT_EQ(HostControl2::Get().ReadFrom(&mmio_).uhs_mode_select(), HostControl2::kUhsModeHs400);
}

TEST_F(SdhciTest, HwReset) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectHwReset();
  dut_->SdmmcHwReset();
  ASSERT_NO_FATAL_FAILURE(mock_sdhci_.VerifyAndClear());
}

TEST_F(SdhciTest, RequestCommandOnly) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

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
      .suppress_error_messages = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };

  Response::Get(0).FromValue(0xf3bbf2c0).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequest(&request));

  auto command = Command::Get().FromValue(0);

  EXPECT_EQ(Argument::Get().ReadFrom(&mmio_).reg_value(), 0x7b7d9fbd);
  EXPECT_EQ(command.ReadFrom(&mmio_).command_index(), SDMMC_SEND_STATUS);
  EXPECT_EQ(command.command_type(), Command::kCommandTypeNormal);
  EXPECT_FALSE(command.data_present());
  EXPECT_TRUE(command.command_index_check());
  EXPECT_TRUE(command.command_crc_check());
  EXPECT_EQ(command.response_type(), Command::kResponseType48Bits);

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
      .suppress_error_messages = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };

  Response::Get(0).FromValue(0x9f93b17d).WriteTo(&mmio_);
  Response::Get(1).FromValue(0x89aaba9e).WriteTo(&mmio_);
  Response::Get(2).FromValue(0xc14b059e).WriteTo(&mmio_);
  Response::Get(3).FromValue(0x7329a9e3).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequest(&request));

  EXPECT_EQ(Argument::Get().ReadFrom(&mmio_).reg_value(), 0x9c1dc1ed);
  EXPECT_EQ(command.ReadFrom(&mmio_).command_index(), SDMMC_SEND_CSD);
  EXPECT_EQ(command.command_type(), Command::kCommandTypeNormal);
  EXPECT_FALSE(command.data_present());
  EXPECT_TRUE(command.command_crc_check());
  EXPECT_EQ(command.response_type(), Command::kResponseType136Bits);

  EXPECT_OK(request.status);
  EXPECT_EQ(request.response[0], 0x9f93b17d);
  EXPECT_EQ(request.response[1], 0x89aaba9e);
  EXPECT_EQ(request.response[2], 0xc14b059e);
  EXPECT_EQ(request.response[3], 0x7329a9e3);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, RequestWithData) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

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
      .virt_buffer = reinterpret_cast<uint8_t*>(buffer),
      .virt_size = 0,
      .buf_offset = 0,
      .pmt = ZX_HANDLE_INVALID,
      .suppress_error_messages = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };

  Response::Get(0).FromValue(0x4ea3f1f3).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequest(&request));

  auto transfer_mode = TransferMode::Get().FromValue(0);
  auto command = Command::Get().FromValue(0);

  EXPECT_EQ(BlockSize::Get().ReadFrom(&mmio_).reg_value(), 16);
  EXPECT_EQ(BlockCount::Get().ReadFrom(&mmio_).reg_value(), 4);
  EXPECT_EQ(Argument::Get().ReadFrom(&mmio_).reg_value(), 0xfc4e6f56);

  EXPECT_TRUE(transfer_mode.ReadFrom(&mmio_).multi_block());
  EXPECT_FALSE(transfer_mode.read());
  EXPECT_EQ(transfer_mode.auto_cmd_enable(), TransferMode::kAutoCmdDisable);
  EXPECT_TRUE(transfer_mode.block_count_enable());
  EXPECT_FALSE(transfer_mode.dma_enable());

  EXPECT_EQ(command.ReadFrom(&mmio_).command_index(), SDMMC_WRITE_MULTIPLE_BLOCK);
  EXPECT_EQ(command.command_type(), Command::kCommandTypeNormal);
  EXPECT_TRUE(command.data_present());
  EXPECT_TRUE(command.command_index_check());
  EXPECT_TRUE(command.command_crc_check());
  EXPECT_EQ(command.response_type(), Command::kResponseType48Bits);

  EXPECT_EQ(BufferData::Get().ReadFrom(&mmio_).reg_value(), 0x6db4a2d1);

  EXPECT_OK(request.status);
  EXPECT_EQ(request.response[0], 0x4ea3f1f3);

  request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS | SDMMC_CMD_AUTO12,
      .arg = 0x55c1c22c,
      .blockcount = 4,
      .blocksize = 16,
      .use_dma = false,
      .dma_vmo = ZX_HANDLE_INVALID,
      .virt_buffer = reinterpret_cast<uint8_t*>(buffer),
      .virt_size = 0,
      .buf_offset = 0,
      .pmt = ZX_HANDLE_INVALID,
      .suppress_error_messages = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };

  Response::Get(0).FromValue(0xa5387c19).WriteTo(&mmio_);
  BufferData::Get().FromValue(0xe99dd637).WriteTo(&mmio_);
  EXPECT_OK(dut_->SdmmcRequest(&request));

  EXPECT_EQ(BlockSize::Get().ReadFrom(&mmio_).reg_value(), 16);
  EXPECT_EQ(BlockCount::Get().ReadFrom(&mmio_).reg_value(), 4);
  EXPECT_EQ(Argument::Get().ReadFrom(&mmio_).reg_value(), 0x55c1c22c);

  EXPECT_TRUE(transfer_mode.ReadFrom(&mmio_).multi_block());
  EXPECT_TRUE(transfer_mode.read());
  EXPECT_EQ(transfer_mode.auto_cmd_enable(), TransferMode::kAutoCmd12);
  EXPECT_TRUE(transfer_mode.block_count_enable());
  EXPECT_FALSE(transfer_mode.dma_enable());

  EXPECT_EQ(command.ReadFrom(&mmio_).command_index(), SDMMC_READ_MULTIPLE_BLOCK);
  EXPECT_EQ(command.command_type(), Command::kCommandTypeNormal);
  EXPECT_TRUE(command.data_present());
  EXPECT_TRUE(command.command_index_check());
  EXPECT_TRUE(command.command_crc_check());
  EXPECT_EQ(command.response_type(), Command::kResponseType48Bits);

  EXPECT_OK(request.status);
  EXPECT_EQ(request.response[0], 0xa5387c19);

  for (uint32_t i = 0; i < 16; i++) {
    EXPECT_EQ(buffer[i], 0xe99dd637);
  }

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, RequestAbort) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  EXPECT_OK(dut_->Init());

  uint32_t buffer[4] = {0x178096fb, 0x27328a47, 0x3267ce33, 0x8fccdf57};

  sdmmc_req_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0,
      .blockcount = 4,
      .blocksize = 4,
      .use_dma = false,
      .dma_vmo = ZX_HANDLE_INVALID,
      .virt_buffer = reinterpret_cast<uint8_t*>(buffer),
      .virt_size = 0,
      .buf_offset = 0,
      .pmt = ZX_HANDLE_INVALID,
      .suppress_error_messages = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };

  dut_->reset_mask();

  EXPECT_OK(dut_->SdmmcRequest(&request));
  EXPECT_EQ(dut_->reset_mask(), 0);

  request.cmd_idx = SDMMC_STOP_TRANSMISSION;
  request.cmd_flags = SDMMC_STOP_TRANSMISSION_FLAGS;
  request.blockcount = 0;
  request.blocksize = 0;
  request.virt_buffer = nullptr;
  EXPECT_OK(dut_->SdmmcRequest(&request));
  EXPECT_EQ(dut_->reset_mask(),
            SoftwareReset::Get().FromValue(0).set_reset_dat(1).set_reset_cmd(1).reg_value());

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DmaRequest64Bit) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 4, 0, &vmo));

  sdmmc_req_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0,
      .blockcount = 4,
      .blocksize = static_cast<uint16_t>(zx_system_get_page_size()),
      .use_dma = true,
      .dma_vmo = vmo.get(),
      .virt_buffer = nullptr,
      .virt_size = 0,
      .buf_offset = 0,
      .pmt = ZX_HANDLE_INVALID,
      .suppress_error_messages = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };
  EXPECT_OK(dut_->SdmmcRequest(&request));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), zx_system_get_page_size());
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const Sdhci::AdmaDescriptor96* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor96*>(dut_->iobuf_virt());

  uint64_t address;
  memcpy(&address, &descriptors[0].address, sizeof(address));
  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(address, zx_system_get_page_size());
  EXPECT_EQ(descriptors[0].length, zx_system_get_page_size());

  EXPECT_EQ(descriptors[1].attr, 0b100'001);
  EXPECT_EQ(descriptors[1].address, zx_system_get_page_size());
  EXPECT_EQ(descriptors[1].length, zx_system_get_page_size());

  memcpy(&address, &descriptors[2].address, sizeof(address));
  EXPECT_EQ(descriptors[2].attr, 0b100'001);
  EXPECT_EQ(address, zx_system_get_page_size());
  EXPECT_EQ(descriptors[2].length, zx_system_get_page_size());

  EXPECT_EQ(descriptors[3].attr, 0b100'011);
  EXPECT_EQ(descriptors[3].address, zx_system_get_page_size());
  EXPECT_EQ(descriptors[3].length, zx_system_get_page_size());

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DmaRequest32Bit) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 4, 0, &vmo));

  sdmmc_req_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0,
      .blockcount = 4,
      .blocksize = static_cast<uint16_t>(zx_system_get_page_size()),
      .use_dma = true,
      .dma_vmo = vmo.get(),
      .virt_buffer = nullptr,
      .virt_size = 0,
      .buf_offset = 0,
      .pmt = ZX_HANDLE_INVALID,
      .suppress_error_messages = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };
  EXPECT_OK(dut_->SdmmcRequest(&request));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), zx_system_get_page_size());
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const Sdhci::AdmaDescriptor64* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor64*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].address, zx_system_get_page_size());
  EXPECT_EQ(descriptors[0].length, zx_system_get_page_size());

  EXPECT_EQ(descriptors[1].attr, 0b100'001);
  EXPECT_EQ(descriptors[1].address, zx_system_get_page_size());
  EXPECT_EQ(descriptors[1].length, zx_system_get_page_size());

  EXPECT_EQ(descriptors[2].attr, 0b100'001);
  EXPECT_EQ(descriptors[2].address, zx_system_get_page_size());
  EXPECT_EQ(descriptors[2].length, zx_system_get_page_size());

  EXPECT_EQ(descriptors[3].attr, 0b100'011);
  EXPECT_EQ(descriptors[3].address, zx_system_get_page_size());
  EXPECT_EQ(descriptors[3].length, zx_system_get_page_size());

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, SdioInBandInterrupt) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  EXPECT_OK(dut_->Init());

  in_band_interrupt_protocol_ops_t callback_ops = {
      .callback = [](void* ctx) -> void {
        sync_completion_signal(reinterpret_cast<sync_completion_t*>(ctx));
      },
  };

  sync_completion_t callback_called;
  in_band_interrupt_protocol_t callback = {
      .ops = &callback_ops,
      .ctx = &callback_called,
  };

  EXPECT_OK(dut_->SdmmcRegisterInBandInterrupt(&callback));

  dut_->TriggerCardInterrupt();
  sync_completion_wait(&callback_called, ZX_TIME_INFINITE);
  sync_completion_reset(&callback_called);

  sdmmc_req_t request = {
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
      .suppress_error_messages = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };
  EXPECT_OK(dut_->SdmmcRequest(&request));

  dut_->SdmmcAckInBandInterrupt();

  // Verify that the card interrupt remains enabled after other interrupts have been disabled, such
  // as after a commend.
  dut_->TriggerCardInterrupt();
  sync_completion_wait(&callback_called, ZX_TIME_INFINITE);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DmaSplitOneBoundary) {
  constexpr zx_paddr_t kDescriptorAddress = 0xc000'0000;
  const zx_paddr_t kStartAddress = 0xa7ff'ffff & ~PageMask();

  ASSERT_NO_FATAL_FAILURE(CreateDut(
      {
          kDescriptorAddress,
          kStartAddress,
          kStartAddress + zx_system_get_page_size(),
          kStartAddress + (zx_system_get_page_size() * 2),
          0xb000'0000,
      },
      SDHCI_QUIRK_USE_DMA_BOUNDARY_ALIGNMENT, 0x0800'0000));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 4, 0, &vmo));

  sdmmc_req_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0,
      .blockcount =
          static_cast<uint16_t>((zx_system_get_page_size() / 8) + 16),  // Two pages plus 256 bytes.
      .blocksize = 16,
      .use_dma = true,
      .dma_vmo = vmo.get(),
      .virt_buffer = nullptr,
      .virt_size = 0,
      .buf_offset = zx_system_get_page_size() -
                    4,  // The first buffer should be split across the 128M boundary.
      .pmt = ZX_HANDLE_INVALID,
      .suppress_error_messages = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };
  EXPECT_OK(dut_->SdmmcRequest(&request));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), kDescriptorAddress);
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const Sdhci::AdmaDescriptor64* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor64*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].address, 0xa7ff'fffc);
  EXPECT_EQ(descriptors[0].length, 4);

  EXPECT_EQ(descriptors[1].attr, 0b100'001);
  EXPECT_EQ(descriptors[1].address, 0xa800'0000);
  EXPECT_EQ(descriptors[1].length, zx_system_get_page_size() * 2);

  EXPECT_EQ(descriptors[2].attr, 0b100'011);
  EXPECT_EQ(descriptors[2].address, 0xb000'0000);
  EXPECT_EQ(descriptors[2].length, 256 - 4);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DmaSplitManyBoundaries) {
  constexpr zx_paddr_t kDescriptorAddress = 0xc000'0000;
  ASSERT_NO_FATAL_FAILURE(CreateDut(
      {
          kDescriptorAddress,
          0xabcd'0000,
      },
      SDHCI_QUIRK_USE_DMA_BOUNDARY_ALIGNMENT, 0x100));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));

  sdmmc_req_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0,
      .blockcount = 64,
      .blocksize = 16,
      .use_dma = true,
      .dma_vmo = vmo.get(),
      .virt_buffer = nullptr,
      .virt_size = 0,
      .buf_offset = 128,
      .pmt = ZX_HANDLE_INVALID,
      .suppress_error_messages = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };
  EXPECT_OK(dut_->SdmmcRequest(&request));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), kDescriptorAddress);
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const Sdhci::AdmaDescriptor64* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor64*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].address, 0xabcd'0080);
  EXPECT_EQ(descriptors[0].length, 128);

  EXPECT_EQ(descriptors[1].attr, 0b100'001);
  EXPECT_EQ(descriptors[1].address, 0xabcd'0100);
  EXPECT_EQ(descriptors[1].length, 256);

  EXPECT_EQ(descriptors[2].attr, 0b100'001);
  EXPECT_EQ(descriptors[2].address, 0xabcd'0200);
  EXPECT_EQ(descriptors[2].length, 256);

  EXPECT_EQ(descriptors[3].attr, 0b100'001);
  EXPECT_EQ(descriptors[3].address, 0xabcd'0300);
  EXPECT_EQ(descriptors[3].length, 256);

  EXPECT_EQ(descriptors[4].attr, 0b100'011);
  EXPECT_EQ(descriptors[4].address, 0xabcd'0400);
  EXPECT_EQ(descriptors[4].length, 128);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DmaNoBoundaries) {
  constexpr zx_paddr_t kDescriptorAddress = 0xc000'0000;
  const zx_paddr_t kStartAddress = 0xa7ff'ffff & ~PageMask();

  ASSERT_NO_FATAL_FAILURE(CreateDut({
      kDescriptorAddress,
      kStartAddress,
      kStartAddress + zx_system_get_page_size(),
      kStartAddress + (zx_system_get_page_size() * 2),
      0xb000'0000,
  }));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 4, 0, &vmo));

  sdmmc_req_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0,
      .blockcount = static_cast<uint16_t>((zx_system_get_page_size() / 8) + 16),
      .blocksize = 16,
      .use_dma = true,
      .dma_vmo = vmo.get(),
      .virt_buffer = nullptr,
      .virt_size = 0,
      .buf_offset = zx_system_get_page_size() - 4,
      .pmt = ZX_HANDLE_INVALID,
      .suppress_error_messages = 0,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };
  EXPECT_OK(dut_->SdmmcRequest(&request));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), kDescriptorAddress);
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const Sdhci::AdmaDescriptor64* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor64*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].address, 0xa7ff'fffc);
  EXPECT_EQ(descriptors[0].length, (zx_system_get_page_size() * 2) + 4);

  EXPECT_EQ(descriptors[1].attr, 0b100'011);
  EXPECT_EQ(descriptors[1].address, 0xb000'0000);
  EXPECT_EQ(descriptors[1].length, 256 - 4);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DmaRequest64BitScatterGather) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  for (int i = 0; i < 4; i++) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(512 * 16, 0, &vmo));
    EXPECT_OK(dut_->SdmmcRegisterVmo(i, 3, std::move(vmo), 64 * i, 512 * 12, SDMMC_VMO_RIGHT_READ));
  }

  const sdmmc_buffer_region_t buffers[4] = {
      {
          .buffer =
              {
                  .vmo_id = 1,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 16,
          .size = 512,
      },
      {
          .buffer =
              {
                  .vmo_id = 0,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 32,
          .size = 512 * 3,
      },
      {
          .buffer =
              {
                  .vmo_id = 3,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 48,
          .size = 512 * 10,
      },
      {
          .buffer =
              {
                  .vmo_id = 2,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 80,
          .size = 512 * 7,
      },
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 512,
      .suppress_error_messages = false,
      .client_id = 3,
      .buffers_list = buffers,
      .buffers_count = std::size(buffers),
  };
  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), zx_system_get_page_size());
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const auto* const descriptors = reinterpret_cast<Sdhci::AdmaDescriptor96*>(dut_->iobuf_virt());

  uint64_t address;
  memcpy(&address, &descriptors[0].address, sizeof(address));
  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(address, zx_system_get_page_size() + 80);
  EXPECT_EQ(descriptors[0].length, 512);

  memcpy(&address, &descriptors[1].address, sizeof(address));
  EXPECT_EQ(descriptors[1].attr, 0b100'001);
  EXPECT_EQ(address, zx_system_get_page_size() + 32);
  EXPECT_EQ(descriptors[1].length, 512 * 3);

  // Buffer is greater than one page and gets split across two descriptors.
  memcpy(&address, &descriptors[2].address, sizeof(address));
  EXPECT_EQ(descriptors[2].attr, 0b100'001);
  EXPECT_EQ(address, zx_system_get_page_size() + 240);
  EXPECT_EQ(descriptors[2].length, zx_system_get_page_size() - 240);

  memcpy(&address, &descriptors[3].address, sizeof(address));
  EXPECT_EQ(descriptors[3].attr, 0b100'001);
  EXPECT_EQ(address, zx_system_get_page_size());
  EXPECT_EQ(descriptors[3].length, (512 * 10) - zx_system_get_page_size() + 240);

  memcpy(&address, &descriptors[4].address, sizeof(address));
  EXPECT_EQ(descriptors[4].attr, 0b100'011);
  EXPECT_EQ(address, zx_system_get_page_size() + 208);
  EXPECT_EQ(descriptors[4].length, 512 * 7);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DmaRequest32BitScatterGather) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  for (int i = 0; i < 4; i++) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(512 * 16, 0, &vmo));
    EXPECT_OK(
        dut_->SdmmcRegisterVmo(i, 3, std::move(vmo), 64 * i, 512 * 12, SDMMC_VMO_RIGHT_WRITE));
  }

  const sdmmc_buffer_region_t buffers[4] = {
      {
          .buffer =
              {
                  .vmo_id = 1,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 16,
          .size = 512,
      },
      {
          .buffer =
              {
                  .vmo_id = 0,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 32,
          .size = 512 * 3,
      },
      {
          .buffer =
              {
                  .vmo_id = 3,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 48,
          .size = 512 * 10,
      },
      {
          .buffer =
              {
                  .vmo_id = 2,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 80,
          .size = 512 * 7,
      },
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 512,
      .suppress_error_messages = false,
      .client_id = 3,
      .buffers_list = buffers,
      .buffers_count = std::size(buffers),
  };
  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), zx_system_get_page_size());
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const auto* const descriptors = reinterpret_cast<Sdhci::AdmaDescriptor64*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].address, zx_system_get_page_size() + 80);
  EXPECT_EQ(descriptors[0].length, 512);

  EXPECT_EQ(descriptors[1].attr, 0b100'001);
  EXPECT_EQ(descriptors[1].address, zx_system_get_page_size() + 32);
  EXPECT_EQ(descriptors[1].length, 512 * 3);

  // Buffer is greater than one page and gets split across two descriptors.
  EXPECT_EQ(descriptors[2].attr, 0b100'001);
  EXPECT_EQ(descriptors[2].address, zx_system_get_page_size() + 240);
  EXPECT_EQ(descriptors[2].length, zx_system_get_page_size() - 240);

  EXPECT_EQ(descriptors[3].attr, 0b100'001);
  EXPECT_EQ(descriptors[3].address, zx_system_get_page_size());
  EXPECT_EQ(descriptors[3].length, (512 * 10) - zx_system_get_page_size() + 240);

  EXPECT_EQ(descriptors[4].attr, 0b100'011);
  EXPECT_EQ(descriptors[4].address, zx_system_get_page_size() + 208);
  EXPECT_EQ(descriptors[4].length, 512 * 7);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DmaSplitOneBoundaryScatterGather) {
  constexpr zx_paddr_t kDescriptorAddress = 0xc000'0000;
  const zx_paddr_t kStartAddress = 0xa7ff'ffff & ~PageMask();

  ASSERT_NO_FATAL_FAILURE(CreateDut(
      {
          kDescriptorAddress,
          kStartAddress,
          kStartAddress + zx_system_get_page_size(),
          kStartAddress + (zx_system_get_page_size() * 2),
          0xb000'0000,
      },
      SDHCI_QUIRK_USE_DMA_BOUNDARY_ALIGNMENT, 0x0800'0000));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 4, 0, &vmo));
  ASSERT_OK(dut_->SdmmcRegisterVmo(0, 0, std::move(vmo), 0, zx_system_get_page_size() * 4,
                                   SDMMC_VMO_RIGHT_WRITE));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo_id = 0,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      // The first buffer should be split across the 128M boundary.
      .offset = zx_system_get_page_size() - 4,
      // Two pages plus 256 bytes.
      .size = (zx_system_get_page_size() * 2) + 256,
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 16,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), kDescriptorAddress);
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const Sdhci::AdmaDescriptor64* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor64*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].address, 0xa7ff'fffc);
  EXPECT_EQ(descriptors[0].length, 4);

  EXPECT_EQ(descriptors[1].attr, 0b100'001);
  EXPECT_EQ(descriptors[1].address, 0xa800'0000);
  EXPECT_EQ(descriptors[1].length, zx_system_get_page_size() * 2);

  EXPECT_EQ(descriptors[2].attr, 0b100'011);
  EXPECT_EQ(descriptors[2].address, 0xb000'0000);
  EXPECT_EQ(descriptors[2].length, 256 - 4);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DmaSplitManyBoundariesScatterGather) {
  constexpr zx_paddr_t kDescriptorAddress = 0xc000'0000;
  ASSERT_NO_FATAL_FAILURE(CreateDut(
      {
          kDescriptorAddress,
          0xabcd'0000,
      },
      SDHCI_QUIRK_USE_DMA_BOUNDARY_ALIGNMENT, 0x100));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(dut_->SdmmcRegisterVmo(0, 0, std::move(vmo), 0, zx_system_get_page_size(),
                                   SDMMC_VMO_RIGHT_WRITE));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo_id = 0,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = 128,
      .size = 16 * 64,
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 16,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), kDescriptorAddress);
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const Sdhci::AdmaDescriptor64* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor64*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].address, 0xabcd'0080);
  EXPECT_EQ(descriptors[0].length, 128);

  EXPECT_EQ(descriptors[1].attr, 0b100'001);
  EXPECT_EQ(descriptors[1].address, 0xabcd'0100);
  EXPECT_EQ(descriptors[1].length, 256);

  EXPECT_EQ(descriptors[2].attr, 0b100'001);
  EXPECT_EQ(descriptors[2].address, 0xabcd'0200);
  EXPECT_EQ(descriptors[2].length, 256);

  EXPECT_EQ(descriptors[3].attr, 0b100'001);
  EXPECT_EQ(descriptors[3].address, 0xabcd'0300);
  EXPECT_EQ(descriptors[3].length, 256);

  EXPECT_EQ(descriptors[4].attr, 0b100'011);
  EXPECT_EQ(descriptors[4].address, 0xabcd'0400);
  EXPECT_EQ(descriptors[4].length, 128);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DmaNoBoundariesScatterGather) {
  constexpr zx_paddr_t kDescriptorAddress = 0xc000'0000;
  const zx_paddr_t kStartAddress = 0xa7ff'ffff & ~PageMask();

  ASSERT_NO_FATAL_FAILURE(CreateDut({
      kDescriptorAddress,
      kStartAddress,
      kStartAddress + zx_system_get_page_size(),
      kStartAddress + (zx_system_get_page_size() * 2),
      0xb000'0000,
  }));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 4, 0, &vmo));
  ASSERT_OK(dut_->SdmmcRegisterVmo(0, 0, std::move(vmo), 0, zx_system_get_page_size() * 4,
                                   SDMMC_VMO_RIGHT_WRITE));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo_id = 0,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = zx_system_get_page_size() - 4,
      .size = (zx_system_get_page_size() * 2) + 256,
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 16,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), kDescriptorAddress);
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const Sdhci::AdmaDescriptor64* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor64*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].address, 0xa7ff'fffc);
  EXPECT_EQ(descriptors[0].length, (zx_system_get_page_size() * 2) + 4);

  EXPECT_EQ(descriptors[1].attr, 0b100'011);
  EXPECT_EQ(descriptors[1].address, 0xb000'0000);
  EXPECT_EQ(descriptors[1].length, 256 - 4);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, CommandSettingsScatterGatherMultiBlock) {
  ASSERT_NO_FATAL_FAILURE(CreateDut(SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  EXPECT_OK(dut_->SdmmcRegisterVmo(0, 0, std::move(vmo), 0, zx_system_get_page_size(),
                                   SDMMC_VMO_RIGHT_READ));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo_id = 0,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = 0,
      .size = 1024,
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234'abcd,
      .blocksize = 512,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };

  Response::Get(0).FromValue(0).set_reg_value(0xabcd'1234).WriteTo(&mmio_);
  Response::Get(1).FromValue(0).set_reg_value(0xa5a5'a5a5).WriteTo(&mmio_);
  Response::Get(2).FromValue(0).set_reg_value(0x1122'3344).WriteTo(&mmio_);
  Response::Get(3).FromValue(0).set_reg_value(0xaabb'ccdd).WriteTo(&mmio_);

  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(response[0], 0xabcd'1234);
  EXPECT_EQ(response[1], 0);
  EXPECT_EQ(response[2], 0);
  EXPECT_EQ(response[3], 0);

  const Command command = Command::Get().ReadFrom(&mmio_);
  EXPECT_EQ(command.response_type(), Command::kResponseType48Bits);
  EXPECT_TRUE(command.command_crc_check());
  EXPECT_TRUE(command.command_index_check());
  EXPECT_TRUE(command.data_present());
  EXPECT_EQ(command.command_type(), Command::kCommandTypeNormal);
  EXPECT_EQ(command.command_index(), SDMMC_WRITE_MULTIPLE_BLOCK);

  const TransferMode transfer_mode = TransferMode::Get().ReadFrom(&mmio_);
  EXPECT_TRUE(transfer_mode.dma_enable());
  EXPECT_TRUE(transfer_mode.block_count_enable());
  EXPECT_EQ(transfer_mode.auto_cmd_enable(), TransferMode::kAutoCmdDisable);
  EXPECT_FALSE(transfer_mode.read());
  EXPECT_TRUE(transfer_mode.multi_block());

  EXPECT_EQ(BlockSize::Get().ReadFrom(&mmio_).reg_value(), 512);
  EXPECT_EQ(BlockCount::Get().ReadFrom(&mmio_).reg_value(), 2);
  EXPECT_EQ(Argument::Get().ReadFrom(&mmio_).reg_value(), 0x1234'abcd);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, CommandSettingsScatterGatherSingleBlock) {
  ASSERT_NO_FATAL_FAILURE(CreateDut(SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  EXPECT_OK(dut_->SdmmcRegisterVmo(0, 0, std::move(vmo), 0, zx_system_get_page_size(),
                                   SDMMC_VMO_RIGHT_WRITE));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo_id = 0,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = 0,
      .size = 128,
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_BLOCK,
      .cmd_flags = SDMMC_READ_BLOCK_FLAGS,
      .arg = 0x1234'abcd,
      .blocksize = 128,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };

  Response::Get(0).FromValue(0).set_reg_value(0xabcd'1234).WriteTo(&mmio_);
  Response::Get(1).FromValue(0).set_reg_value(0xa5a5'a5a5).WriteTo(&mmio_);
  Response::Get(2).FromValue(0).set_reg_value(0x1122'3344).WriteTo(&mmio_);
  Response::Get(3).FromValue(0).set_reg_value(0xaabb'ccdd).WriteTo(&mmio_);

  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(response[0], 0xabcd'1234);
  EXPECT_EQ(response[1], 0);
  EXPECT_EQ(response[2], 0);
  EXPECT_EQ(response[3], 0);

  const Command command = Command::Get().ReadFrom(&mmio_);
  EXPECT_EQ(command.response_type(), Command::kResponseType48Bits);
  EXPECT_TRUE(command.command_crc_check());
  EXPECT_TRUE(command.command_index_check());
  EXPECT_TRUE(command.data_present());
  EXPECT_EQ(command.command_type(), Command::kCommandTypeNormal);
  EXPECT_EQ(command.command_index(), SDMMC_READ_BLOCK);

  const TransferMode transfer_mode = TransferMode::Get().ReadFrom(&mmio_);
  EXPECT_TRUE(transfer_mode.dma_enable());
  EXPECT_FALSE(transfer_mode.block_count_enable());
  EXPECT_EQ(transfer_mode.auto_cmd_enable(), TransferMode::kAutoCmdDisable);
  EXPECT_TRUE(transfer_mode.read());
  EXPECT_FALSE(transfer_mode.multi_block());

  EXPECT_EQ(BlockSize::Get().ReadFrom(&mmio_).reg_value(), 128);
  EXPECT_EQ(BlockCount::Get().ReadFrom(&mmio_).reg_value(), 1);
  EXPECT_EQ(Argument::Get().ReadFrom(&mmio_).reg_value(), 0x1234'abcd);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, CommandSettingsScatterGatherBusyResponse) {
  ASSERT_NO_FATAL_FAILURE(CreateDut(SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  const sdmmc_req_new_t request = {
      .cmd_idx = 55,
      .cmd_flags = SDMMC_RESP_LEN_48B | SDMMC_CMD_TYPE_NORMAL | SDMMC_RESP_CRC_CHECK |
                   SDMMC_RESP_CMD_IDX_CHECK,
      .arg = 0x1234'abcd,
      .blocksize = 0,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = nullptr,
      .buffers_count = 0,
  };

  Response::Get(0).FromValue(0).set_reg_value(0xabcd'1234).WriteTo(&mmio_);
  Response::Get(1).FromValue(0).set_reg_value(0xa5a5'a5a5).WriteTo(&mmio_);
  Response::Get(2).FromValue(0).set_reg_value(0x1122'3344).WriteTo(&mmio_);
  Response::Get(3).FromValue(0).set_reg_value(0xaabb'ccdd).WriteTo(&mmio_);

  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(response[0], 0xabcd'1234);
  EXPECT_EQ(response[1], 0);
  EXPECT_EQ(response[2], 0);
  EXPECT_EQ(response[3], 0);

  const Command command = Command::Get().ReadFrom(&mmio_);
  EXPECT_EQ(command.response_type(), Command::kResponseType48BitsWithBusy);
  EXPECT_TRUE(command.command_crc_check());
  EXPECT_TRUE(command.command_index_check());
  EXPECT_FALSE(command.data_present());
  EXPECT_EQ(command.command_type(), Command::kCommandTypeNormal);
  EXPECT_EQ(command.command_index(), 55);

  const TransferMode transfer_mode = TransferMode::Get().ReadFrom(&mmio_);
  EXPECT_FALSE(transfer_mode.dma_enable());
  EXPECT_FALSE(transfer_mode.block_count_enable());
  EXPECT_EQ(transfer_mode.auto_cmd_enable(), TransferMode::kAutoCmdDisable);
  EXPECT_FALSE(transfer_mode.read());
  EXPECT_FALSE(transfer_mode.multi_block());

  EXPECT_EQ(BlockSize::Get().ReadFrom(&mmio_).reg_value(), 0);
  EXPECT_EQ(BlockCount::Get().ReadFrom(&mmio_).reg_value(), 0);
  EXPECT_EQ(Argument::Get().ReadFrom(&mmio_).reg_value(), 0x1234'abcd);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, ScatterGatherZeroBlockSize) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  for (int i = 0; i < 4; i++) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(512 * 16, 0, &vmo));
    EXPECT_OK(dut_->SdmmcRegisterVmo(i, 3, std::move(vmo), 64 * i, 512 * 12, SDMMC_VMO_RIGHT_READ));
  }

  const sdmmc_buffer_region_t buffers[4] = {
      {
          .buffer =
              {
                  .vmo_id = 1,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 16,
          .size = 512,
      },
      {
          .buffer =
              {
                  .vmo_id = 0,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 32,
          .size = 512 * 3,
      },
      {
          .buffer =
              {
                  .vmo_id = 3,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 48,
          .size = 512 * 10,
      },
      {
          .buffer =
              {
                  .vmo_id = 2,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 80,
          .size = 512 * 7,
      },
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 0,
      .suppress_error_messages = false,
      .client_id = 3,
      .buffers_list = buffers,
      .buffers_count = std::size(buffers),
  };
  uint32_t response[4] = {};
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, ScatterGatherNoBuffers) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(512 * 16, 0, &vmo));
  EXPECT_OK(dut_->SdmmcRegisterVmo(1, 3, std::move(vmo), 0, 1024,
                                   SDMMC_VMO_RIGHT_READ | SDMMC_VMO_RIGHT_WRITE));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo_id = 1,
          },
      .type = SDMMC_BUFFER_TYPE_VMO_ID,
      .offset = 0,
      .size = 512,
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 0,
      .suppress_error_messages = false,
      .client_id = 3,
      .buffers_list = &buffer,
      .buffers_count = 0,
  };
  uint32_t response[4] = {};
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, OwnedAndUnownedBuffers) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmos[4];
  for (int i = 0; i < 4; i++) {
    ASSERT_OK(zx::vmo::create(512 * 16, 0, &vmos[i]));
    if (i % 2 == 0) {
      EXPECT_OK(
          dut_->SdmmcRegisterVmo(i, 3, std::move(vmos[i]), 64 * i, 512 * 12, SDMMC_VMO_RIGHT_READ));
    }
  }

  const sdmmc_buffer_region_t buffers[4] = {
      {
          .buffer =
              {
                  .vmo = vmos[1].get(),
              },
          .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
          .offset = 16,
          .size = 512,
      },
      {
          .buffer =
              {
                  .vmo_id = 0,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 32,
          .size = 512 * 3,
      },
      {
          .buffer =
              {
                  .vmo = vmos[3].get(),
              },
          .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
          .offset = 48,
          .size = 512 * 10,
      },
      {
          .buffer =
              {
                  .vmo_id = 2,
              },
          .type = SDMMC_BUFFER_TYPE_VMO_ID,
          .offset = 80,
          .size = 512 * 7,
      },
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 512,
      .suppress_error_messages = false,
      .client_id = 3,
      .buffers_list = buffers,
      .buffers_count = std::size(buffers),
  };

  EXPECT_NO_FAILURES(ExpectPmoCount(3));

  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  // Unowned buffers should have been unpinned.
  EXPECT_NO_FAILURES(ExpectPmoCount(3));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), zx_system_get_page_size());
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const auto* const descriptors = reinterpret_cast<Sdhci::AdmaDescriptor96*>(dut_->iobuf_virt());

  uint64_t address;
  memcpy(&address, &descriptors[0].address, sizeof(address));
  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(address, zx_system_get_page_size() + 16);
  EXPECT_EQ(descriptors[0].length, 512);

  memcpy(&address, &descriptors[1].address, sizeof(address));
  EXPECT_EQ(descriptors[1].attr, 0b100'001);
  EXPECT_EQ(address, zx_system_get_page_size() + 32);
  EXPECT_EQ(descriptors[1].length, 512 * 3);

  // Buffer is greater than one page and gets split across two descriptors.
  memcpy(&address, &descriptors[2].address, sizeof(address));
  EXPECT_EQ(descriptors[2].attr, 0b100'001);
  EXPECT_EQ(address, zx_system_get_page_size() + 48);
  EXPECT_EQ(descriptors[2].length, zx_system_get_page_size() - 48);

  memcpy(&address, &descriptors[3].address, sizeof(address));
  EXPECT_EQ(descriptors[3].attr, 0b100'001);
  EXPECT_EQ(address, zx_system_get_page_size());
  EXPECT_EQ(descriptors[3].length, (512 * 10) - zx_system_get_page_size() + 48);

  memcpy(&address, &descriptors[4].address, sizeof(address));
  EXPECT_EQ(descriptors[4].attr, 0b100'011);
  EXPECT_EQ(address, zx_system_get_page_size() + 208);
  EXPECT_EQ(descriptors[4].length, 512 * 7);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, CombineContiguousRegions) {
  constexpr zx_paddr_t kDescriptorAddress = 0xc000'0000;
  const zx_paddr_t kStartAddress = 0xa7ff'ffff & ~PageMask();

  ASSERT_NO_FATAL_FAILURE(CreateDut({
      kDescriptorAddress,
      kStartAddress,
      kStartAddress + zx_system_get_page_size(),
      kStartAddress + (zx_system_get_page_size() * 2),
      kStartAddress + (zx_system_get_page_size() * 3),
      0xb000'0000,
  }));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create((zx_system_get_page_size() * 4) + 512, 0, &vmo));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 512,
      .size = zx_system_get_page_size() * 4,
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 512,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };

  EXPECT_NO_FAILURES(ExpectPmoCount(1));

  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_NO_FAILURES(ExpectPmoCount(1));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), kDescriptorAddress);
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const Sdhci::AdmaDescriptor64* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor64*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].address, kStartAddress + 512);
  EXPECT_EQ(descriptors[0].length, (zx_system_get_page_size() * 4) - 512);

  EXPECT_EQ(descriptors[1].attr, 0b100'011);
  EXPECT_EQ(descriptors[1].address, 0xb000'0000);
  EXPECT_EQ(descriptors[1].length, 512);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DiscontiguousRegions) {
  constexpr zx_paddr_t kDescriptorAddress = 0xc000'0000;
  constexpr zx_paddr_t kDiscontiguousPageOffset = 0x1'0000'0000;
  const zx_paddr_t kStartAddress = 0xa7ff'ffff & ~PageMask();

  ASSERT_NO_FATAL_FAILURE(CreateDut({
      kDescriptorAddress,
      kStartAddress,
      kDiscontiguousPageOffset + kStartAddress,
      (2 * kDiscontiguousPageOffset) + kStartAddress,
      (3 * kDiscontiguousPageOffset) + kStartAddress,
      (4 * kDiscontiguousPageOffset) + kStartAddress,
      (4 * kDiscontiguousPageOffset) + kStartAddress + zx_system_get_page_size(),
      (4 * kDiscontiguousPageOffset) + kStartAddress + (2 * zx_system_get_page_size()),
      (5 * kDiscontiguousPageOffset) + kStartAddress,
      (6 * kDiscontiguousPageOffset) + kStartAddress,
      (7 * kDiscontiguousPageOffset) + kStartAddress,
      (7 * kDiscontiguousPageOffset) + kStartAddress + zx_system_get_page_size(),
      (8 * kDiscontiguousPageOffset) + kStartAddress,
  }));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 12, 0, &vmo));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 512,
      .size = (zx_system_get_page_size() * 12) - 512 - 1024,
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 512,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };

  EXPECT_NO_FAILURES(ExpectPmoCount(1));

  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_NO_FAILURES(ExpectPmoCount(1));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), kDescriptorAddress);
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const auto* const descriptors = reinterpret_cast<Sdhci::AdmaDescriptor96*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].get_address(), kStartAddress + 512);
  EXPECT_EQ(descriptors[0].length, zx_system_get_page_size() - 512);

  EXPECT_EQ(descriptors[1].attr, 0b100'001);
  EXPECT_EQ(descriptors[1].get_address(), kDiscontiguousPageOffset + kStartAddress);
  EXPECT_EQ(descriptors[1].length, zx_system_get_page_size());

  EXPECT_EQ(descriptors[2].attr, 0b100'001);
  EXPECT_EQ(descriptors[2].get_address(), (2 * kDiscontiguousPageOffset) + kStartAddress);
  EXPECT_EQ(descriptors[2].length, zx_system_get_page_size());

  EXPECT_EQ(descriptors[3].attr, 0b100'001);
  EXPECT_EQ(descriptors[3].get_address(), (3 * kDiscontiguousPageOffset) + kStartAddress);
  EXPECT_EQ(descriptors[3].length, zx_system_get_page_size());

  EXPECT_EQ(descriptors[4].attr, 0b100'001);
  EXPECT_EQ(descriptors[4].get_address(), (4 * kDiscontiguousPageOffset) + kStartAddress);
  EXPECT_EQ(descriptors[4].length, zx_system_get_page_size() * 3);

  EXPECT_EQ(descriptors[5].attr, 0b100'001);
  EXPECT_EQ(descriptors[5].get_address(), (5 * kDiscontiguousPageOffset) + kStartAddress);
  EXPECT_EQ(descriptors[5].length, zx_system_get_page_size());

  EXPECT_EQ(descriptors[6].attr, 0b100'001);
  EXPECT_EQ(descriptors[6].get_address(), (6 * kDiscontiguousPageOffset) + kStartAddress);
  EXPECT_EQ(descriptors[6].length, zx_system_get_page_size());

  EXPECT_EQ(descriptors[7].attr, 0b100'001);
  EXPECT_EQ(descriptors[7].get_address(), (7 * kDiscontiguousPageOffset) + kStartAddress);
  EXPECT_EQ(descriptors[7].length, zx_system_get_page_size() * 2);

  EXPECT_EQ(descriptors[8].attr, 0b100'011);
  EXPECT_EQ(descriptors[8].get_address(), (8 * kDiscontiguousPageOffset) + kStartAddress);
  EXPECT_EQ(descriptors[8].length, zx_system_get_page_size() - 1024);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, RegionStartAndEndOffsets) {
  constexpr zx_paddr_t kDescriptorAddress = 0xc000'0000;
  const zx_paddr_t kStartAddress = 0xa7ff'ffff & ~PageMask();

  ASSERT_NO_FATAL_FAILURE(CreateDut({
      kDescriptorAddress,
      kStartAddress,
      kStartAddress + zx_system_get_page_size(),
      kStartAddress + (zx_system_get_page_size() * 2),
      kStartAddress + (zx_system_get_page_size() * 3),
  }));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create((zx_system_get_page_size() * 4), 0, &vmo));

  sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 0,
      .size = zx_system_get_page_size(),
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 512,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };

  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  const Sdhci::AdmaDescriptor64* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor64*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'011);
  EXPECT_EQ(descriptors[0].address, kStartAddress);
  EXPECT_EQ(descriptors[0].length, zx_system_get_page_size());

  buffer.offset = 512;
  buffer.size = zx_system_get_page_size() - 512;

  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(descriptors[0].attr, 0b100'011);
  EXPECT_EQ(descriptors[0].address, kStartAddress + zx_system_get_page_size() + 512);
  EXPECT_EQ(descriptors[0].length, zx_system_get_page_size() - 512);

  buffer.offset = 0;
  buffer.size = zx_system_get_page_size() - 512;

  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(descriptors[0].attr, 0b100'011);
  EXPECT_EQ(descriptors[0].address, kStartAddress + (zx_system_get_page_size() * 2));
  EXPECT_EQ(descriptors[0].length, zx_system_get_page_size() - 512);

  buffer.offset = 512;
  buffer.size = zx_system_get_page_size() - 1024;

  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(descriptors[0].attr, 0b100'011);
  EXPECT_EQ(descriptors[0].address, kStartAddress + (zx_system_get_page_size() * 3) + 512);
  EXPECT_EQ(descriptors[0].length, zx_system_get_page_size() - 1024);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, BufferZeroSize) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(0)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 4, 0, &vmo));
    EXPECT_OK(dut_->SdmmcRegisterVmo(1, 0, std::move(vmo), 0, zx_system_get_page_size() * 4,
                                     SDMMC_VMO_RIGHT_READ));
  }

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 4, 0, &vmo));

  {
    const sdmmc_buffer_region_t buffers[3] = {
        {
            .buffer =
                {
                    .vmo_id = 1,
                },
            .type = SDMMC_BUFFER_TYPE_VMO_ID,
            .offset = 0,
            .size = 512,
        },
        {
            .buffer =
                {
                    .vmo = vmo.get(),
                },
            .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
            .offset = 0,
            .size = 0,
        },
        {
            .buffer =
                {
                    .vmo_id = 1,
                },
            .type = SDMMC_BUFFER_TYPE_VMO_ID,
            .offset = 512,
            .size = 512,
        },
    };

    const sdmmc_req_new_t request = {
        .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
        .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
        .arg = 0x1234abcd,
        .blocksize = 512,
        .suppress_error_messages = false,
        .client_id = 0,
        .buffers_list = buffers,
        .buffers_count = std::size(buffers),
    };

    uint32_t response[4] = {};
    EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
  }

  {
    const sdmmc_buffer_region_t buffers[3] = {
        {
            .buffer =
                {
                    .vmo = vmo.get(),
                },
            .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
            .offset = 0,
            .size = 512,
        },
        {
            .buffer =
                {
                    .vmo_id = 1,
                },
            .type = SDMMC_BUFFER_TYPE_VMO_ID,
            .offset = 0,
            .size = 0,
        },
        {
            .buffer =
                {
                    .vmo = vmo.get(),
                },
            .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
            .offset = 512,
            .size = 512,
        },
    };

    const sdmmc_req_new_t request = {
        .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
        .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
        .arg = 0x1234abcd,
        .blocksize = 512,
        .suppress_error_messages = false,
        .client_id = 0,
        .buffers_list = buffers,
        .buffers_count = std::size(buffers),
    };

    uint32_t response[4] = {};
    EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));
  }

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, TransferError) {
  ASSERT_NO_FATAL_FAILURE(CreateDut());

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(512, 0, &vmo));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 0,
      .size = 512,
  };
  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 512,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };

  dut_->InjectTransferError();
  uint32_t response[4] = {};
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, MaxTransferSize) {
  std::vector<zx_paddr_t> bti_paddrs;
  bti_paddrs.push_back(0x1000'0000'0000'0000);

  for (size_t i = 0; i < 512; i++) {
    // 512 pages, fully discontiguous.
    bti_paddrs.push_back(zx_system_get_page_size() * (i + 1) * 2);
  }

  ASSERT_NO_FATAL_FAILURE(CreateDut(std::move(bti_paddrs)));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(512, 0, &vmo));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 0,
      .size = 512 * zx_system_get_page_size(),
  };
  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 512,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };

  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  const Sdhci::AdmaDescriptor96* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor96*>(dut_->iobuf_virt());

  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].get_address(), zx_system_get_page_size() * 2);
  EXPECT_EQ(descriptors[0].length, zx_system_get_page_size());

  EXPECT_EQ(descriptors[511].attr, 0b100'011);
  EXPECT_EQ(descriptors[511].get_address(), zx_system_get_page_size() * 2 * 512);
  EXPECT_EQ(descriptors[511].length, zx_system_get_page_size());

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, TransferSizeExceeded) {
  std::vector<zx_paddr_t> bti_paddrs;
  bti_paddrs.push_back(0x1000'0000'0000'0000);

  for (size_t i = 0; i < 513; i++) {
    bti_paddrs.push_back(zx_system_get_page_size() * (i + 1) * 2);
  }

  ASSERT_NO_FATAL_FAILURE(CreateDut(std::move(bti_paddrs)));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(512, 0, &vmo));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 0,
      .size = 513 * zx_system_get_page_size(),
  };
  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 512,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };

  uint32_t response[4] = {};
  EXPECT_NOT_OK(dut_->SdmmcRequestNew(&request, response));

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(SdhciTest, DmaSplitSizeAndAligntmentBoundaries) {
  constexpr zx_paddr_t kDescriptorAddress = 0xc000'0000;
  std::vector<zx_paddr_t> paddrs;
  // Generate a single contiguous physical region.
  paddrs.push_back(kDescriptorAddress);
  for (zx_paddr_t p = 0x1'0001'8000; p < 0x1'0010'0000; p += zx_system_get_page_size()) {
    paddrs.push_back(p);
  }

  ASSERT_NO_FATAL_FAILURE(
      CreateDut(std::move(paddrs), SDHCI_QUIRK_USE_DMA_BOUNDARY_ALIGNMENT, 0x2'0000));

  mock_sdhci_.ExpectGetBaseClock(100'000'000);
  Capabilities0::Get()
      .FromValue(0)
      .set_adma2_support(1)
      .set_v3_64_bit_system_address_support(1)
      .WriteTo(&mmio_);
  EXPECT_OK(dut_->Init());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(1024, 0, &vmo));

  const sdmmc_buffer_region_t buffer = {
      .buffer =
          {
              .vmo = vmo.get(),
          },
      .type = SDMMC_BUFFER_TYPE_VMO_HANDLE,
      .offset = 0x1'8000,
      .size = 0x4'0000,
  };

  const sdmmc_req_new_t request = {
      .cmd_idx = SDMMC_READ_MULTIPLE_BLOCK,
      .cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS,
      .arg = 0x1234abcd,
      .blocksize = 512,
      .suppress_error_messages = false,
      .client_id = 0,
      .buffers_list = &buffer,
      .buffers_count = 1,
  };
  uint32_t response[4] = {};
  EXPECT_OK(dut_->SdmmcRequestNew(&request, response));

  EXPECT_EQ(AdmaSystemAddress::Get(0).ReadFrom(&mmio_).reg_value(), kDescriptorAddress);
  EXPECT_EQ(AdmaSystemAddress::Get(1).ReadFrom(&mmio_).reg_value(), 0);

  const Sdhci::AdmaDescriptor96* const descriptors =
      reinterpret_cast<Sdhci::AdmaDescriptor96*>(dut_->iobuf_virt());

  // Region split due to alignment.
  EXPECT_EQ(descriptors[0].attr, 0b100'001);
  EXPECT_EQ(descriptors[0].get_address(), 0x1'0001'8000);
  EXPECT_EQ(descriptors[0].length, 0x8000);

  // Region split due to both alignment and descriptor max size.
  EXPECT_EQ(descriptors[1].attr, 0b100'001);
  EXPECT_EQ(descriptors[1].get_address(), 0x1'0002'0000);
  EXPECT_EQ(descriptors[1].length, 0);  // Zero length -> 0x1'0000 bytes
  EXPECT_EQ(descriptors[2].attr, 0b100'001);

  // Region split due to descriptor max size.
  EXPECT_EQ(descriptors[2].get_address(), 0x1'0003'0000);
  EXPECT_EQ(descriptors[2].length, 0);

  EXPECT_EQ(descriptors[3].attr, 0b100'001);
  EXPECT_EQ(descriptors[3].get_address(), 0x1'0004'0000);
  EXPECT_EQ(descriptors[3].length, 0);

  EXPECT_EQ(descriptors[4].attr, 0b100'011);
  EXPECT_EQ(descriptors[4].get_address(), 0x1'0005'0000);
  EXPECT_EQ(descriptors[4].length, 0x8000);

  dut_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

}  // namespace sdhci
