// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-sdmmc.h"

#include <lib/fit/defer.h>

#include <atomic>

#include <fbl/condition_variable.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace {

constexpr size_t kRegisterCount = 139;

}  // namespace

namespace sdmmc {

class MtkSdmmcTest : public MtkSdmmc, public ddk::InBandInterruptProtocol<MtkSdmmcTest> {
 public:
  MtkSdmmcTest(ddk_mock::MockMmioRegRegion& registers, bool is_sdio = false)
      : MtkSdmmc(nullptr, ddk::MmioBuffer(registers.GetMmioBuffer()), zx::bti(ZX_HANDLE_INVALID),
                 kNullHostInfo, zx::interrupt(ZX_HANDLE_INVALID), ddk::GpioProtocolClient(),
                 ddk::GpioProtocolClient(),
                 board_mt8167::MtkSdmmcConfig{
                     .fifo_depth = 128, .src_clk_freq = 200000000, .is_sdio = is_sdio}),
        interrupt_count_(0),
        callbacks_received_(0) {}

  MtkSdmmcTest(zx_device_t* parent, ddk_mock::MockMmioRegRegion& registers, bool is_sdio = false)
      : MtkSdmmc(parent, ddk::MmioBuffer(registers.GetMmioBuffer()), zx::bti(ZX_HANDLE_INVALID),
                 kNullHostInfo, zx::interrupt(ZX_HANDLE_INVALID), ddk::GpioProtocolClient(),
                 ddk::GpioProtocolClient(),
                 board_mt8167::MtkSdmmcConfig{
                     .fifo_depth = 128, .src_clk_freq = 200000000, .is_sdio = is_sdio}),
        interrupt_count_(0),
        callbacks_received_(0) {}

  zx_status_t RegisterInBandInterrupt() {
    in_band_interrupt_protocol_t callback{&in_band_interrupt_protocol_ops_, this};
    return MtkSdmmc::SdmmcRegisterInBandInterrupt(&callback);
  }

  void TriggerInterrupts(uint32_t count) {
    fbl::AutoLock mutex_al(&mutex_);
    interrupt_count_ += count;
  }

  void WaitForCallbacks(uint32_t count) {
    for (uint32_t i = 0; i < count; i += callbacks_received_.exchange(0)) {
      sync_completion_wait(&callback_completion_, ZX_TIME_INFINITE);
      sync_completion_reset(&callback_completion_);
    }
  }

  void StopIrqThread() {
    thread_stop_ = true;
    JoinIrqThread();
  }

  void InBandInterruptCallback() {
    callbacks_received_++;
    sync_completion_signal(&callback_completion_);
  }

 protected:
  zx_status_t WaitForInterrupt(zx::time* timestamp) override {
    while (!thread_stop_) {
      fbl::AutoLock mutex_al(&mutex_);
      if (interrupt_count_ > 0) {
        interrupt_count_--;
        return ZX_OK;
      } else if (req_ != nullptr) {
        return ZX_OK;
      }
    }

    return ZX_ERR_CANCELED;
  }

  volatile bool thread_stop_ = false;

 private:
  static constexpr sdmmc_host_info_t kNullHostInfo{
      .caps = 0, .max_transfer_size = 0, .max_transfer_size_non_dma = 0, .prefs = 0};

  sync_completion_t callback_completion_;
  uint32_t interrupt_count_ TA_GUARDED(mutex_);
  std::atomic<uint32_t> callbacks_received_;
};

template <class T>
ddk_mock::MockMmioReg& GetMockReg(ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get().addr()];
}

template <class T>
ddk_mock::MockMmioReg& GetMockReg(int index, ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get(index).addr()];
}

void VerifyAll(ddk_mock::MockMmioReg* registers) {
  for (size_t i = 0; i < kRegisterCount; i++) {
    registers[i].VerifyAndClear();
  }
}

TEST(SdmmcTest, SetBusWidth) {
  ddk_mock::MockMmioReg reg_array[kRegisterCount];
  ddk_mock::MockMmioRegRegion mock_regs(reg_array, sizeof(uint32_t), kRegisterCount);
  MtkSdmmcTest sdmmc(mock_regs);

  GetMockReg<SdcCfg>(mock_regs).ExpectWrite(SdcCfg().set_bus_width(SdcCfg::kBusWidth4).reg_value());
  EXPECT_OK(sdmmc.SdmmcSetBusWidth(SDMMC_BUS_WIDTH_FOUR));
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  GetMockReg<SdcCfg>(mock_regs).ExpectWrite(SdcCfg().set_bus_width(SdcCfg::kBusWidth1).reg_value());
  EXPECT_OK(sdmmc.SdmmcSetBusWidth(SDMMC_BUS_WIDTH_ONE));
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  GetMockReg<SdcCfg>(mock_regs).ExpectWrite(SdcCfg().set_bus_width(SdcCfg::kBusWidth8).reg_value());
  EXPECT_OK(sdmmc.SdmmcSetBusWidth(SDMMC_BUS_WIDTH_EIGHT));
  mock_regs.VerifyAll();
}

TEST(SdmmcTest, SetBusFreq) {
  ddk_mock::MockMmioReg reg_array[kRegisterCount];
  ddk_mock::MockMmioRegRegion mock_regs(reg_array, sizeof(uint32_t), kRegisterCount);
  MtkSdmmcTest sdmmc(mock_regs);

  // 400 kHz: Use divider value 125.
  auto msdc_cfg = MsdcCfg().set_card_ck_mode(MsdcCfg::kCardCkModeNoDiv);
  GetMockReg<MsdcCfg>(mock_regs)
      .ExpectRead(msdc_cfg.reg_value())
      .ExpectWrite()
      .ExpectWrite(
          msdc_cfg.set_card_ck_mode(MsdcCfg::kCardCkModeDiv).set_card_ck_div(125).reg_value())
      .ExpectRead(msdc_cfg.set_card_ck_stable(1).reg_value())
      .ExpectWrite(msdc_cfg.set_ck_pwr_down(1).set_ck_drive(1).reg_value());

  EXPECT_OK(sdmmc.SdmmcSetBusFreq(400000));
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  // DDR 1 MHz: Use divider value 25.
  msdc_cfg.set_reg_value(0).set_card_ck_mode(MsdcCfg::kCardCkModeDdr);
  GetMockReg<MsdcCfg>(mock_regs)
      .ExpectRead(msdc_cfg.reg_value())
      .ExpectWrite()
      .ExpectWrite(msdc_cfg.set_card_ck_div(25).reg_value())
      .ExpectRead(msdc_cfg.set_card_ck_stable(1).reg_value())
      .ExpectWrite(msdc_cfg.set_ck_pwr_down(1).set_ck_drive(1).reg_value());

  EXPECT_OK(sdmmc.SdmmcSetBusFreq(1000000));
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  // SDR 200 MHz: No divider.
  msdc_cfg.set_reg_value(0).set_card_ck_mode(MsdcCfg::kCardCkModeDiv).set_card_ck_div(50);
  GetMockReg<MsdcCfg>(mock_regs)
      .ExpectRead(msdc_cfg.reg_value())
      .ExpectWrite()
      .ExpectWrite(
          msdc_cfg.set_card_ck_mode(MsdcCfg::kCardCkModeNoDiv).set_card_ck_div(0).reg_value())
      .ExpectRead(msdc_cfg.set_card_ck_stable(1).reg_value())
      .ExpectWrite(msdc_cfg.set_ck_pwr_down(1).set_ck_drive(1).reg_value());

  EXPECT_OK(sdmmc.SdmmcSetBusFreq(200000000));
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  // HS400 mode @ 200 MHz: No divider.
  msdc_cfg.set_reg_value(0).set_card_ck_mode(MsdcCfg::kCardCkModeHs400);
  GetMockReg<MsdcCfg>(mock_regs)
      .ExpectRead(msdc_cfg.reg_value())
      .ExpectWrite()
      .ExpectWrite(msdc_cfg.set_hs400_ck_mode(1).reg_value())
      .ExpectRead(msdc_cfg.set_card_ck_stable(1).reg_value())
      .ExpectWrite(msdc_cfg.set_ck_pwr_down(1).set_ck_drive(1).reg_value());

  EXPECT_OK(sdmmc.SdmmcSetBusFreq(200000000));
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  // HS400 mode @ 10 MHz: Use divider value 3.
  msdc_cfg.set_reg_value(0).set_card_ck_mode(MsdcCfg::kCardCkModeHs400).set_hs400_ck_mode(1);
  GetMockReg<MsdcCfg>(mock_regs)
      .ExpectRead(msdc_cfg.reg_value())
      .ExpectWrite()
      .ExpectWrite(msdc_cfg.set_card_ck_div(3).set_hs400_ck_mode(0).reg_value())
      .ExpectRead(msdc_cfg.set_card_ck_stable(1).reg_value())
      .ExpectWrite(msdc_cfg.set_ck_pwr_down(1).set_ck_drive(1).reg_value());

  EXPECT_OK(sdmmc.SdmmcSetBusFreq(10000000));
  mock_regs.VerifyAll();

  // Zero bus frequency: Gate the clock.
  msdc_cfg.set_reg_value(0).set_ck_pwr_down(1).set_ck_drive(1);
  GetMockReg<MsdcCfg>(mock_regs)
      .ExpectRead(msdc_cfg.reg_value())
      .ExpectWrite(msdc_cfg.set_ck_pwr_down(0).set_ck_drive(0).reg_value());

  EXPECT_OK(sdmmc.SdmmcSetBusFreq(0));
  mock_regs.VerifyAll();
}

TEST(SdmmcTest, SetTiming) {
  ddk_mock::MockMmioReg reg_array[kRegisterCount];
  ddk_mock::MockMmioRegRegion mock_regs(reg_array, sizeof(uint32_t), kRegisterCount);
  MtkSdmmcTest sdmmc(mock_regs);

  auto msdc_cfg = MsdcCfg().set_card_ck_mode(MsdcCfg::kCardCkModeDiv);
  GetMockReg<MsdcCfg>(mock_regs)
      .ExpectRead(msdc_cfg.reg_value())
      .ExpectWrite()
      .ExpectRead()
      .ExpectWrite(msdc_cfg.set_card_ck_mode(MsdcCfg::kCardCkModeDdr).reg_value())
      .ExpectRead(msdc_cfg.set_card_ck_stable(1).reg_value());

  EXPECT_OK(sdmmc.SdmmcSetTiming(SDMMC_TIMING_HSDDR));
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  msdc_cfg.set_reg_value(0).set_card_ck_mode(MsdcCfg::kCardCkModeDiv);
  GetMockReg<MsdcCfg>(mock_regs)
      .ExpectRead(msdc_cfg.reg_value())
      .ExpectWrite()
      .ExpectRead()
      .ExpectWrite(msdc_cfg.set_card_ck_mode(MsdcCfg::kCardCkModeHs400).reg_value())
      .ExpectRead(msdc_cfg.set_card_ck_stable(1).reg_value());

  EXPECT_OK(sdmmc.SdmmcSetTiming(SDMMC_TIMING_HS400));
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  msdc_cfg.set_reg_value(0).set_card_ck_mode(MsdcCfg::kCardCkModeHs400);
  GetMockReg<MsdcCfg>(mock_regs)
      .ExpectRead(msdc_cfg.reg_value())
      .ExpectWrite()
      .ExpectRead()
      .ExpectWrite(msdc_cfg.set_card_ck_mode(MsdcCfg::kCardCkModeDiv).reg_value())
      .ExpectRead(msdc_cfg.set_card_ck_stable(1).reg_value());

  EXPECT_OK(sdmmc.SdmmcSetTiming(SDMMC_TIMING_HS200));
  mock_regs.VerifyAll();
}

TEST(SdmmcTest, SetTimingNoSdioUhs104) {
  ddk_mock::MockMmioReg reg_array[kRegisterCount];
  ddk_mock::MockMmioRegRegion mock_regs(reg_array, sizeof(uint32_t), kRegisterCount);
  MtkSdmmcTest sdmmc(mock_regs, true);

  GetMockReg<MsdcCfg>(mock_regs).ReadReturns(MsdcCfg().set_card_ck_stable(1).reg_value());

  EXPECT_OK(sdmmc.SdmmcSetTiming(SDMMC_TIMING_DDR50));
  EXPECT_NOT_OK(sdmmc.SdmmcSetTiming(SDMMC_TIMING_SDR104));
  EXPECT_OK(sdmmc.SdmmcSetTiming(SDMMC_TIMING_SDR50));
  EXPECT_OK(sdmmc.SdmmcSetTiming(SDMMC_TIMING_HS));
  EXPECT_OK(sdmmc.SdmmcSetTiming(SDMMC_TIMING_LEGACY));
}

TEST(SdmmcTest, Request) {
  ddk_mock::MockMmioReg reg_array[kRegisterCount];
  ddk_mock::MockMmioRegRegion mock_regs(reg_array, sizeof(uint32_t), kRegisterCount);
  MtkSdmmcTest sdmmc(mock_regs);

  auto thread_cleanup = fit::defer([&sdmmc] { sdmmc.StopIrqThread(); });

  // Set card_ck_stable so Init() can call SdmmcSetBusFreq() without hanging.
  GetMockReg<MsdcCfg>(mock_regs).ReadReturns(MsdcCfg().set_card_ck_stable(1).reg_value());
  sdmmc.Init();
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  // Command with no response.
  sdmmc_req_t req;
  memset(&req, 0, sizeof(req));
  req.cmd_idx = 50;
  req.cmd_flags = SDMMC_RESP_NONE | SDMMC_CMD_TYPE_NORMAL;
  req.arg = 0x1234abcd;
  req.status = 1;

  GetMockReg<MsdcInt>(mock_regs).ExpectRead(MsdcInt().set_cmd_ready(1).reg_value());
  GetMockReg<SdcArg>(mock_regs).ExpectWrite(req.arg);
  GetMockReg<SdcCmd>(mock_regs).ExpectWrite(req.cmd_idx);

  EXPECT_OK(sdmmc.SdmmcRequest(&req));
  EXPECT_OK(req.status);
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  // Command with response R1.
  req.cmd_idx = 19;
  req.cmd_flags = SDMMC_RESP_R1 | SDMMC_CMD_TYPE_NORMAL;
  req.arg = 0x55555555;
  req.status = 1;

  GetMockReg<MsdcInt>(mock_regs).ExpectRead(MsdcInt().set_cmd_ready(1).reg_value());
  GetMockReg<SdcArg>(mock_regs).ExpectWrite(req.arg);
  GetMockReg<SdcCmd>(mock_regs).ExpectWrite(
      SdcCmd().set_cmd(req.cmd_idx).set_resp_type(SdcCmd::kRespTypeR1).reg_value());
  GetMockReg<SdcResponse>(0, mock_regs).ExpectRead(0x1234abcd);

  EXPECT_OK(sdmmc.SdmmcRequest(&req));
  EXPECT_OK(req.status);
  EXPECT_EQ(req.response[0], 0x1234abcd);
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  // Command with response R2.
  req.cmd_idx = 22;
  req.cmd_flags = SDMMC_RESP_R2 | SDMMC_CMD_TYPE_NORMAL;
  req.arg = 0x12345678;
  req.status = 1;
  GetMockReg<MsdcInt>(mock_regs).ExpectRead(MsdcInt().set_cmd_ready(1).reg_value());
  GetMockReg<SdcArg>(mock_regs).ExpectWrite(req.arg);
  GetMockReg<SdcCmd>(mock_regs).ExpectWrite(
      SdcCmd().set_cmd(req.cmd_idx).set_resp_type(SdcCmd::kRespTypeR2).reg_value());
  GetMockReg<SdcResponse>(0, mock_regs).ExpectRead(0x0a0a0a0a);
  GetMockReg<SdcResponse>(1, mock_regs).ExpectRead(0x50505050);
  GetMockReg<SdcResponse>(2, mock_regs).ExpectRead(0x1234abcd);
  GetMockReg<SdcResponse>(3, mock_regs).ExpectRead(0xfedcba98);

  EXPECT_OK(sdmmc.SdmmcRequest(&req));
  EXPECT_OK(req.status);
  EXPECT_EQ(req.response[0], 0x0a0a0a0a);
  EXPECT_EQ(req.response[1], 0x50505050);
  EXPECT_EQ(req.response[2], 0x1234abcd);
  EXPECT_EQ(req.response[3], 0xfedcba98);
  mock_regs.VerifyAll();
}

TEST(SdmmcTest, ReadPolled) {
  ddk_mock::MockMmioReg reg_array[kRegisterCount];
  ddk_mock::MockMmioRegRegion mock_regs(reg_array, sizeof(uint32_t), kRegisterCount);
  MtkSdmmcTest sdmmc(mock_regs);

  auto thread_cleanup = fit::defer([&sdmmc] { sdmmc.StopIrqThread(); });

  GetMockReg<MsdcCfg>(mock_regs).ReadReturns(MsdcCfg().set_card_ck_stable(1).reg_value());
  sdmmc.Init();
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  // Single block read.
  constexpr uint8_t single_block_data[16] = {0x12, 0xc2, 0x1c, 0x63, 0x54, 0x51, 0x7e, 0xf3,
                                             0x0a, 0x1b, 0xa5, 0x2a, 0xca, 0x23, 0x02, 0x82};
  uint8_t single_block_buf[sizeof(single_block_data)] = {0};

  sdmmc_req_t req;
  memset(&req, 0, sizeof(req));
  req.cmd_idx = 8;
  req.cmd_flags = SDMMC_RESP_R1 | SDMMC_CMD_TYPE_NORMAL | SDMMC_RESP_DATA_PRESENT | SDMMC_CMD_READ;
  req.arg = 0x72b2af17;
  req.status = 1;
  req.blockcount = 1;
  req.blocksize = sizeof(single_block_buf);
  req.virt_buffer = single_block_buf;
  req.virt_size = sizeof(single_block_buf);

  GetMockReg<MsdcInt>(mock_regs).ExpectRead(MsdcInt().set_cmd_ready(1).reg_value());
  GetMockReg<SdcArg>(mock_regs).ExpectWrite(req.arg);
  GetMockReg<SdcCmd>(mock_regs).ExpectWrite(SdcCmd()
                                                .set_cmd(req.cmd_idx)
                                                .set_resp_type(SdcCmd::kRespTypeR1)
                                                .set_block_type(SdcCmd::kBlockTypeSingle)
                                                .set_block_size(sizeof(single_block_buf))
                                                .reg_value());
  GetMockReg<SdcBlockNum>(mock_regs).ExpectWrite(1);
  GetMockReg<SdcResponse>(0, mock_regs).ExpectRead(0x80dcd8ff);
  GetMockReg<MsdcCfg>(mock_regs).ExpectWrite(MsdcCfg().set_pio_mode(1).reg_value());
  GetMockReg<MsdcFifoCs>(mock_regs).ExpectRead().ExpectWrite().ExpectRead(0).ExpectRead(
      MsdcFifoCs().set_rx_fifo_count(sizeof(single_block_data)).reg_value());

  for (size_t i = 0; i < sizeof(single_block_data); i++) {
    GetMockReg<MsdcRxData>(mock_regs).ExpectRead(single_block_data[i]);
  }

  EXPECT_OK(sdmmc.SdmmcRequest(&req));
  EXPECT_OK(req.status);
  EXPECT_EQ(req.response[0], 0x80dcd8ff);
  EXPECT_BYTES_EQ(single_block_data, single_block_buf, sizeof(single_block_data));
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  // Multi block read.
  constexpr uint8_t multi_block_data[64] = {
      0x99, 0x5b, 0xd9, 0x80, 0x35, 0x5e, 0xb9, 0x92, 0x07, 0xd2, 0x11, 0xd7, 0x72,
      0xb3, 0x61, 0x7b, 0xf8, 0x5a, 0x65, 0xf1, 0x43, 0x4d, 0x43, 0x78, 0x67, 0x67,
      0xd6, 0xd4, 0x3f, 0x0a, 0x1a, 0x93, 0x0f, 0x77, 0x71, 0x1b, 0xc6, 0x5a, 0x38,
      0xc0, 0xcd, 0x5f, 0x03, 0x63, 0x5f, 0xa6, 0x78, 0xb2, 0xf6, 0xdb, 0x00, 0x0e,
      0xd4, 0xf3, 0xe3, 0x69, 0xf2, 0x8e, 0x25, 0xaa, 0x6f, 0xbc, 0xe6, 0xba};
  uint8_t multi_block_buf[sizeof(multi_block_data)] = {0};

  req.cmd_idx = 36;
  req.cmd_flags = SDMMC_RESP_R1 | SDMMC_CMD_TYPE_NORMAL | SDMMC_RESP_DATA_PRESENT | SDMMC_CMD_READ |
                  SDMMC_CMD_MULTI_BLK | SDMMC_CMD_AUTO12;
  req.arg = 0x954887c8;
  req.status = 1;
  req.blockcount = 4;
  req.blocksize = sizeof(multi_block_buf) / 4;
  req.virt_buffer = multi_block_buf;
  req.virt_size = sizeof(multi_block_buf);

  GetMockReg<MsdcInt>(mock_regs).ExpectRead(MsdcInt().set_cmd_ready(1).reg_value());
  GetMockReg<SdcArg>(mock_regs).ExpectWrite(req.arg);
  GetMockReg<SdcCmd>(mock_regs).ExpectWrite(SdcCmd()
                                                .set_cmd(req.cmd_idx)
                                                .set_resp_type(SdcCmd::kRespTypeR1)
                                                .set_block_type(SdcCmd::kBlockTypeMulti)
                                                .set_block_size(sizeof(multi_block_buf) / 4)
                                                .set_auto_cmd(SdcCmd::kAutoCmd12)
                                                .reg_value());
  GetMockReg<SdcBlockNum>(mock_regs).ExpectWrite(4);
  GetMockReg<SdcResponse>(0, mock_regs).ExpectRead(0xaa30091e);
  GetMockReg<MsdcCfg>(mock_regs).ExpectWrite(MsdcCfg().set_pio_mode(1).reg_value());
  GetMockReg<MsdcFifoCs>(mock_regs)
      .ExpectRead()
      .ExpectWrite()
      .ExpectRead(0)
      .ExpectRead(MsdcFifoCs().set_rx_fifo_count(sizeof(multi_block_data) / 4).reg_value())
      .ExpectRead(0)
      .ExpectRead(0)
      .ExpectRead(MsdcFifoCs().set_rx_fifo_count(sizeof(multi_block_data) / 4).reg_value())
      .ExpectRead(MsdcFifoCs().set_rx_fifo_count(sizeof(multi_block_data) / 4).reg_value())
      .ExpectRead(0)
      .ExpectRead(0)
      .ExpectRead(0)
      .ExpectRead(MsdcFifoCs().set_rx_fifo_count(sizeof(multi_block_data) / 4).reg_value());

  for (size_t i = 0; i < sizeof(multi_block_data); i++) {
    GetMockReg<MsdcRxData>(mock_regs).ExpectRead(multi_block_data[i]);
  }

  EXPECT_OK(sdmmc.SdmmcRequest(&req));
  EXPECT_OK(req.status);
  EXPECT_EQ(req.response[0], 0xaa30091e);
  EXPECT_BYTES_EQ(multi_block_data, multi_block_buf, sizeof(multi_block_data));
  mock_regs.VerifyAll();
}

TEST(SdmmcTest, WritePolled) {
  ddk_mock::MockMmioReg reg_array[kRegisterCount];
  ddk_mock::MockMmioRegRegion mock_regs(reg_array, sizeof(uint32_t), kRegisterCount);
  MtkSdmmcTest sdmmc(mock_regs);

  auto thread_cleanup = fit::defer([&sdmmc] { sdmmc.StopIrqThread(); });

  GetMockReg<MsdcCfg>(mock_regs).ReadReturns(MsdcCfg().set_card_ck_stable(1).reg_value());
  sdmmc.Init();
  mock_regs.VerifyAll();

  // Single block read.
  uint8_t single_block_data[16] = {0x12, 0xc2, 0x1c, 0x63, 0x54, 0x51, 0x7e, 0xf3,
                                   0x0a, 0x1b, 0xa5, 0x2a, 0xca, 0x23, 0x02, 0x82};

  sdmmc_req_t req;
  memset(&req, 0, sizeof(req));
  req.cmd_idx = 8;
  req.cmd_flags = SDMMC_RESP_R1 | SDMMC_CMD_TYPE_NORMAL | SDMMC_RESP_DATA_PRESENT;
  req.arg = 0x72b2af17;
  req.status = 1;
  req.blockcount = 1;
  req.blocksize = sizeof(single_block_data);
  req.virt_buffer = single_block_data;
  req.virt_size = sizeof(single_block_data);
  req.buf_offset = 0;

  GetMockReg<MsdcInt>(mock_regs).ExpectRead(MsdcInt().set_cmd_ready(1).reg_value());
  GetMockReg<SdcArg>(mock_regs).ExpectWrite(req.arg);
  GetMockReg<SdcCmd>(mock_regs).ExpectWrite(SdcCmd()
                                                .set_cmd(req.cmd_idx)
                                                .set_resp_type(SdcCmd::kRespTypeR1)
                                                .set_block_type(SdcCmd::kBlockTypeSingle)
                                                .set_block_size(sizeof(single_block_data))
                                                .set_write(1)
                                                .reg_value());
  GetMockReg<SdcBlockNum>(mock_regs).ExpectWrite(1);
  GetMockReg<SdcResponse>(0, mock_regs).ExpectRead(0x80dcd8ff);
  GetMockReg<MsdcCfg>(mock_regs).ExpectWrite(MsdcCfg().set_pio_mode(1).reg_value());
  GetMockReg<MsdcFifoCs>(mock_regs).ExpectRead().ExpectWrite().ExpectRead(0);

  for (size_t i = 0; i < sizeof(single_block_data); i++) {
    GetMockReg<MsdcTxData>(mock_regs).ExpectWrite(single_block_data[i]);
  }

  EXPECT_OK(sdmmc.SdmmcRequest(&req));
  EXPECT_OK(req.status);
  EXPECT_EQ(req.response[0], 0x80dcd8ff);
  ASSERT_NO_FATAL_FAILURES(mock_regs.VerifyAll());

  // Multi block read.
  uint8_t multi_block_data[64] = {0x99, 0x5b, 0xd9, 0x80, 0x35, 0x5e, 0xb9, 0x92, 0x07, 0xd2, 0x11,
                                  0xd7, 0x72, 0xb3, 0x61, 0x7b, 0xf8, 0x5a, 0x65, 0xf1, 0x43, 0x4d,
                                  0x43, 0x78, 0x67, 0x67, 0xd6, 0xd4, 0x3f, 0x0a, 0x1a, 0x93, 0x0f,
                                  0x77, 0x71, 0x1b, 0xc6, 0x5a, 0x38, 0xc0, 0xcd, 0x5f, 0x03, 0x63,
                                  0x5f, 0xa6, 0x78, 0xb2, 0xf6, 0xdb, 0x00, 0x0e, 0xd4, 0xf3, 0xe3,
                                  0x69, 0xf2, 0x8e, 0x25, 0xaa, 0x6f, 0xbc, 0xe6, 0xba};

  req.cmd_idx = 36;
  req.cmd_flags = SDMMC_RESP_R1 | SDMMC_CMD_TYPE_NORMAL | SDMMC_RESP_DATA_PRESENT |
                  SDMMC_CMD_MULTI_BLK | SDMMC_CMD_AUTO12;
  req.arg = 0x954887c8;
  req.status = 1;
  req.blockcount = 4;
  req.blocksize = sizeof(multi_block_data) / 4;
  req.virt_buffer = multi_block_data;
  req.virt_size = sizeof(multi_block_data);

  GetMockReg<MsdcInt>(mock_regs).ExpectRead(MsdcInt().set_cmd_ready(1).reg_value());
  GetMockReg<SdcArg>(mock_regs).ExpectWrite(req.arg);
  GetMockReg<SdcCmd>(mock_regs).ExpectWrite(SdcCmd()
                                                .set_cmd(req.cmd_idx)
                                                .set_resp_type(SdcCmd::kRespTypeR1)
                                                .set_block_type(SdcCmd::kBlockTypeMulti)
                                                .set_block_size(sizeof(multi_block_data) / 4)
                                                .set_auto_cmd(SdcCmd::kAutoCmd12)
                                                .set_write(1)
                                                .reg_value());
  GetMockReg<SdcBlockNum>(mock_regs).ExpectWrite(4);
  GetMockReg<SdcResponse>(0, mock_regs).ExpectRead(0xaa30091e);
  GetMockReg<MsdcCfg>(mock_regs).ExpectWrite(MsdcCfg().set_pio_mode(1).reg_value());
  GetMockReg<MsdcFifoCs>(mock_regs).ExpectRead().ExpectWrite().ExpectRead(0);

  for (size_t i = 0; i < sizeof(multi_block_data); i++) {
    GetMockReg<MsdcTxData>(mock_regs).ExpectWrite(multi_block_data[i]);
  }

  EXPECT_OK(sdmmc.SdmmcRequest(&req));
  EXPECT_OK(req.status);
  EXPECT_EQ(req.response[0], 0xaa30091e);
  mock_regs.VerifyAll();
}

TEST(SdmmcTest, Protocol) {
  ddk_mock::MockMmioReg reg_array[kRegisterCount];
  ddk_mock::MockMmioRegRegion mock_regs(reg_array, sizeof(uint32_t), kRegisterCount);
  MtkSdmmcTest sdmmc(mock_regs);

  ASSERT_EQ(sdmmc.ddk_proto_id_, ZX_PROTOCOL_SDMMC);

  sdmmc_protocol_ops_t* ops = reinterpret_cast<sdmmc_protocol_ops_t*>(sdmmc.ddk_proto_ops_);
  ASSERT_NOT_NULL(ops);
  EXPECT_NOT_NULL(ops->host_info);
  EXPECT_NOT_NULL(ops->set_signal_voltage);
  EXPECT_NOT_NULL(ops->set_bus_width);
  EXPECT_NOT_NULL(ops->set_bus_freq);
  EXPECT_NOT_NULL(ops->set_timing);
  EXPECT_NOT_NULL(ops->hw_reset);
  EXPECT_NOT_NULL(ops->perform_tuning);
  EXPECT_NOT_NULL(ops->request);
}

TEST(SdmmcTest, IrqCallbackCalled) {
  ddk_mock::MockMmioReg reg_array[kRegisterCount];
  ddk_mock::MockMmioRegRegion mock_regs(reg_array, sizeof(uint32_t), kRegisterCount);
  MtkSdmmcTest sdmmc(mock_regs, true);

  ASSERT_OK(sdmmc.RegisterInBandInterrupt());

  auto thread_cleanup = fit::defer([&sdmmc] { sdmmc.StopIrqThread(); });

  GetMockReg<MsdcCfg>(mock_regs).ReadReturns(MsdcCfg().set_card_ck_stable(1).reg_value());
  sdmmc.Init();
  mock_regs.VerifyAll();

  GetMockReg<MsdcInt>(mock_regs)
      .ExpectRead(MsdcInt().set_sdio_irq(1).reg_value())
      .ExpectRead(MsdcInt().set_sdio_irq(0).reg_value())
      .ExpectRead(MsdcInt().set_sdio_irq(1).reg_value())
      .ExpectRead(MsdcInt().set_sdio_irq(0).reg_value())
      .ExpectRead(MsdcInt().set_sdio_irq(1).reg_value());

  sdmmc.TriggerInterrupts(5);
  sdmmc.WaitForCallbacks(3);

  mock_regs.VerifyAll();
}

}  // namespace sdmmc
