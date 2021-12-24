// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/spi/drivers/intel-gspi/intel-gspi.h"

#include <fuchsia/hardware/pci/cpp/banjo-mock.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/device.h>
#include <lib/zx/clock.h>
#include <zircon/types.h>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "fuchsia/hardware/spiimpl/cpp/banjo.h"
#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/spi/drivers/intel-gspi/registers.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

// This is a fairly naive implementation of the MMIO interface offered by the GSPI device. We don't
// care about most of the (configuration) registers, but we implement the basic FIFO registers and
// the status register.
class IntelGspiTest : public zxtest::Test {
 public:
  IntelGspiTest()
      : loop_(&kAsyncLoopConfigNeverAttachToThread),
        region_(ddk_fake::FakeMmioRegRegion(registers_, 4, std::size(registers_))),
        parent_(MockDevice::FakeRootParent()) {}

  void CreateDevice(bool with_interrupt) {
    zx::interrupt duplicate;
    if (with_interrupt) {
      ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq_));
      ASSERT_OK(irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate));
    }

    acpi_.SetGetBusId(
        [](acpi::mock::Device::GetBusIdRequestView,
           acpi::mock::Device::GetBusIdCompleter::Sync& completer) { completer.ReplySuccess(0); });

    auto& status = GetReg(gspi::INTEL_GSPI_SSSR);
    status.SetReadCallback([this]() { return StatusRead(); });
    status.SetWriteCallback([this](uint64_t val) { StatusWrite(val); });

    auto& fifo = GetReg(gspi::INTEL_GSPI_SSDR);
    fifo.SetReadCallback([this]() { return FifoRead(); });
    fifo.SetWriteCallback([this](uint64_t val) { FifoWrite(val); });

    auto& rx_fifo_ctrl = GetReg(gspi::INTEL_GSPI_SIRF);
    rx_fifo_ctrl.SetReadCallback([this]() { return RxFifoCtrlRead(); });
    rx_fifo_ctrl.SetWriteCallback([this](uint64_t val) { RxFifoCtrlWrite(val); });

    auto& tx_fifo_ctrl = GetReg(gspi::INTEL_GSPI_SITF);
    tx_fifo_ctrl.SetReadCallback([this]() { return TxFifoCtrlRead(); });
    tx_fifo_ctrl.SetWriteCallback([this](uint64_t val) { TxFifoCtrlWrite(val); });

    auto& con0 = GetReg(gspi::INTEL_GSPI_SSCR0);
    con0.SetReadCallback([this]() { return con0_reg_.reg_value(); });
    con0.SetWriteCallback([this](uint64_t val) { con0_reg_.set_reg_value(val); });

    auto client = acpi_.CreateClient(loop_.dispatcher());
    ASSERT_OK(client.status_value());
    auto device = std::make_unique<gspi::GspiDevice>(
        parent_.get(), region_.GetMmioBuffer(), std::move(duplicate), std::move(client.value()),
        zx::duration::infinite());
    ASSERT_OK(device->Bind(&device));
    gspi_ = parent_->GetLatestChild();

    ASSERT_EQ(con0_reg_.sse(), 0);
  }

  void TearDown() override {
    if (gspi_) {
      device_async_remove(gspi_);
      ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(parent_.get()));
    }
  }

  uint64_t FifoRead() {
    ZX_ASSERT(con0_reg_.sse() == 1);
    uint64_t val = 0;
    if (fifo_rx_offset_ < rx_data_.size()) {
      val = static_cast<uint64_t>(rx_data_[fifo_rx_offset_]);
      fifo_rx_offset_++;
    }
    if (fifo_rx_offset_ < rx_data_.size()) {
      TriggerIrq();
    }
    return val;
  }

  void FifoWrite(uint64_t val) {
    ZX_ASSERT(con0_reg_.sse() == 1);
    tx_data_.emplace_back(val);
    bytes_transmitted_++;
    TriggerIrq();
  }

  uint64_t StatusRead() {
    auto left_in_fifo = rx_data_.size() - fifo_rx_offset_;
    if (left_in_fifo > rx_fifo_reg_.wmrf()) {
      sts_reg_.set_rfs(1);
    } else {
      sts_reg_.set_rfs(0);
    }

    sts_reg_.set_rne(std::min(bytes_transmitted_, left_in_fifo) != 0);

    // Always say the TX fifo is ready to be serviced, because we don't really have a way for the
    // test to drain the fifo as it's being written to.
    sts_reg_.set_tfs(1);

    return sts_reg_.reg_value();
  }
  void StatusWrite(uint64_t val) { sts_reg_.set_reg_value(sts_reg_.reg_value() & ~val); }

  uint64_t TxFifoCtrlRead() { return tx_fifo_reg_.reg_value(); }
  void TxFifoCtrlWrite(uint64_t val) { tx_fifo_reg_.set_reg_value(val); }

  uint64_t RxFifoCtrlRead() { return rx_fifo_reg_.reg_value(); }
  void RxFifoCtrlWrite(uint64_t val) { rx_fifo_reg_.set_reg_value(val); }

  void TriggerIrq() { irq_.trigger(0, zx::clock::get_monotonic()); }

  ddk_fake::FakeMmioReg& GetReg(uint32_t offset) { return registers_[offset / 4]; }

 protected:
  async::Loop loop_;
  ddk_fake::FakeMmioReg registers_[(gspi::INTEL_GSPI_CAPABILITIES / 4) + 1];
  ddk_fake::FakeMmioRegRegion region_;
  acpi::mock::Device acpi_;
  std::shared_ptr<MockDevice> parent_;
  MockDevice* gspi_;
  zx::interrupt irq_;

  gspi::Con0Reg con0_reg_;
  gspi::TransmitFifoReg tx_fifo_reg_;
  gspi::ReceiveFifoReg rx_fifo_reg_;
  gspi::StatusReg sts_reg_;
  size_t fifo_rx_offset_ = 0;
  // Data that will be received by the test.
  std::vector<uint8_t> rx_data_;
  // Data that was transmitted by the test.
  std::vector<uint8_t> tx_data_;

  // This is a bit of a hack, because the driver drains the RX fifo at the start of each exchange.
  // We don't want this, because the RX fifo is populated at the start of each test run. To work
  // around this, we set the maximum "size" of the RX fifo to be the number of bytes that the driver
  // has transmitted.
  size_t bytes_transmitted_ = 0;
};

TEST_F(IntelGspiTest, TestCreateAndTearDown) { ASSERT_NO_FATAL_FAILURES(CreateDevice(true)); }

TEST_F(IntelGspiTest, TestRx) {
  static const std::vector<uint8_t> kTestData{0xd0, 0x0d, 0xfe, 0xed};

  rx_data_ = kTestData;
  ASSERT_NO_FATAL_FAILURES(CreateDevice(true));
  ddk::SpiImplProtocolClient client(gspi_);
  uint8_t data[4];
  size_t actual;
  ASSERT_OK(client.Exchange(0, nullptr, 0, data, std::size(data), &actual));
  ASSERT_EQ(actual, 4);
  ASSERT_BYTES_EQ(data, kTestData.data(), kTestData.size());
}

TEST_F(IntelGspiTest, TestTx) {
  static const std::vector<uint8_t> kTestData{0xd0, 0x0d, 0xfe, 0xed};
  rx_data_.resize(kTestData.size());
  ASSERT_NO_FATAL_FAILURES(CreateDevice(true));
  ddk::SpiImplProtocolClient client(gspi_);
  size_t actual;
  ASSERT_OK(client.Exchange(0, kTestData.data(), kTestData.size(), nullptr, 0, &actual));
  ASSERT_EQ(tx_data_.size(), 4);
  ASSERT_BYTES_EQ(tx_data_.data(), kTestData.data(), kTestData.size());
}

TEST_F(IntelGspiTest, TestBigTransaction) {
  std::vector<uint8_t> kTestData;
  for (size_t i = 0; i < 128; i++) {
    kTestData.emplace_back(i);
  }

  rx_data_ = kTestData;
  ASSERT_NO_FATAL_FAILURES(CreateDevice(true));
  ddk::SpiImplProtocolClient client(gspi_);
  uint8_t data[128];
  size_t actual;
  ASSERT_OK(client.Exchange(0, kTestData.data(), kTestData.size(), data, std::size(data), &actual));
  ASSERT_EQ(actual, 128);
  ASSERT_BYTES_EQ(data, kTestData.data(), kTestData.size());
  ASSERT_BYTES_EQ(tx_data_.data(), kTestData.data(), kTestData.size());
}
