// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-uart.h"

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/serial/c/banjo.h>
#include <fuchsia/hardware/serial/c/fidl.h>
#include <fuchsia/hardware/serialimpl/async/c/banjo.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/zx/event.h>

#include <vector>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "registers.h"
#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace {

constexpr size_t kDataLen = 32;

class DeviceState {
 public:
  DeviceState() {
    region_.emplace(regs_, sizeof(uint32_t), std::size(regs_));
    // Control register
    regs_[2].SetWriteCallback([=](uint64_t value) {
      control_reg_ = value;
      auto ctrl = Control();
      if (ctrl.rst_rx()) {
        reset_rx_ = true;
      }
      if (ctrl.rst_tx()) {
        reset_tx_ = true;
      }
    });
    regs_[2].SetReadCallback([=]() { return control_reg_; });
    // Status register
    regs_[3].SetWriteCallback([=](uint64_t value) { status_reg_ = value; });
    regs_[3].SetReadCallback([=]() {
      auto status = Status();
      status.set_rx_empty(!rx_len_);
      return status.reg_value();
    });
    // TFIFO
    regs_[0].SetWriteCallback(
        [=](uint64_t value) { tx_buf_.push_back(static_cast<uint8_t>(value)); });
    // RFIFO
    regs_[1].SetReadCallback([=]() {
      uint8_t value = *rx_buf_;
      rx_buf_++;
      rx_len_--;
      return value;
    });
    // Reg5
    regs_[5].SetWriteCallback([=](uint64_t value) { reg5_ = value; });
    regs_[5].SetReadCallback([=]() { return reg5_; });
  }

  void set_irq_signaller(zx::unowned_interrupt signaller) { irq_signaller_ = std::move(signaller); }

  fdf::MmioBuffer GetMmio() { return region_->GetMmioBuffer(); }

  bool PortResetRX() {
    bool reset = reset_rx_;
    reset_rx_ = false;
    return reset;
  }

  bool PortResetTX() {
    bool reset = reset_tx_;
    reset_tx_ = false;
    return reset;
  }

  void Inject(const void* buffer, size_t size) {
    rx_buf_ = static_cast<const unsigned char*>(buffer);
    rx_len_ = size;
    irq_signaller_->trigger(0, zx::time());
  }

  serial::Status Status() {
    return serial::Status::Get().FromValue(static_cast<uint32_t>(status_reg_));
  }

  serial::Control Control() {
    return serial::Control::Get().FromValue(static_cast<uint32_t>(control_reg_));
  }

  serial::Reg5 Reg5() { return serial::Reg5::Get().FromValue(static_cast<uint32_t>(reg5_)); }

  uint32_t StopBits() {
    switch (Control().stop_len()) {
      case 0:
        return SERIAL_STOP_BITS_1;
      case 1:
        return SERIAL_STOP_BITS_2;
      default:
        return 255;
    }
  }

  uint32_t DataBits() {
    switch (Control().xmit_len()) {
      case 0:
        return SERIAL_DATA_BITS_8;
      case 1:
        return SERIAL_DATA_BITS_7;
      case 2:
        return SERIAL_DATA_BITS_6;
      case 3:
        return SERIAL_DATA_BITS_5;
      default:
        return 255;
    }
  }

  uint32_t Parity() {
    switch (Control().parity()) {
      case 0:
        return SERIAL_PARITY_NONE;
      case 2:
        return SERIAL_PARITY_EVEN;
      case 3:
        return SERIAL_PARITY_ODD;
      default:
        return 255;
    }
  }
  bool FlowControl() { return !Control().two_wire(); }

  std::vector<uint8_t> TxBuf() {
    std::vector<uint8_t> buf;
    buf.swap(tx_buf_);
    return buf;
  }

 private:
  std::vector<uint8_t> tx_buf_;
  const uint8_t* rx_buf_ = nullptr;
  size_t rx_len_ = 0;
  bool reset_tx_ = false;
  bool reset_rx_ = false;
  uint64_t reg5_ = 0;
  uint64_t control_reg_ = 0;
  uint64_t status_reg_ = 0;
  ddk_fake::FakeMmioReg regs_[6];
  std::optional<ddk_fake::FakeMmioRegRegion> region_;
  zx::unowned_interrupt irq_signaller_;
};

class AmlUartHarness : public zxtest::Test {
 public:
  void SetUp() override {
    static constexpr serial_port_info_t kSerialInfo = {
        .serial_class = fuchsia_hardware_serial_Class_BLUETOOTH_HCI,
        .serial_vid = PDEV_VID_BROADCOM,
        .serial_pid = PDEV_PID_BCM43458,
    };
    fake_parent_->SetMetadata(DEVICE_METADATA_SERIAL_PORT_INFO, &kSerialInfo, sizeof(kSerialInfo));

    state_.set_irq_signaller(pdev_.CreateVirtualInterrupt(0));
    fake_parent_->AddProtocol(ZX_PROTOCOL_PDEV, pdev_.proto()->ops, pdev_.proto()->ctx);
    auto uart = std::make_unique<serial::AmlUart>(fake_parent_.get(), pdev_.proto(), kSerialInfo,
                                                  state_.GetMmio());
    zx_status_t status = uart->Init();
    ASSERT_OK(status);
    device_ = uart.get();
    // The AmlUart* is now owned by the fake_ddk.
    uart.release();
  }

  serial::AmlUart& Device() { return *device_; }

  DeviceState& device_state() { return state_; }

 private:
  std::shared_ptr<MockDevice> fake_parent_ = MockDevice::FakeRootParent();
  DeviceState state_;
  fake_pdev::FakePDev pdev_;
  serial::AmlUart* device_;
};

TEST_F(AmlUartHarness, SerialImplAsyncGetInfo) {
  serial_port_info_t info;
  ASSERT_OK(Device().SerialImplAsyncGetInfo(&info));
  ASSERT_EQ(info.serial_class, fuchsia_hardware_serial_Class_BLUETOOTH_HCI);
  ASSERT_EQ(info.serial_pid, PDEV_PID_BCM43458);
  ASSERT_EQ(info.serial_vid, PDEV_VID_BROADCOM);
}

TEST_F(AmlUartHarness, SerialImplAsyncConfig) {
  ASSERT_OK(Device().SerialImplAsyncEnable(false));
  ASSERT_EQ(device_state().Control().tx_enable(), 0);
  ASSERT_EQ(device_state().Control().rx_enable(), 0);
  ASSERT_EQ(device_state().Control().inv_cts(), 0);
  static constexpr uint32_t serial_test_config =
      SERIAL_DATA_BITS_6 | SERIAL_STOP_BITS_2 | SERIAL_PARITY_EVEN | SERIAL_FLOW_CTRL_CTS_RTS;
  ASSERT_OK(Device().SerialImplAsyncConfig(20, serial_test_config));
  ASSERT_EQ(device_state().DataBits(), SERIAL_DATA_BITS_6);
  ASSERT_EQ(device_state().StopBits(), SERIAL_STOP_BITS_2);
  ASSERT_EQ(device_state().Parity(), SERIAL_PARITY_EVEN);
  ASSERT_TRUE(device_state().FlowControl());
  ASSERT_OK(Device().SerialImplAsyncConfig(40, SERIAL_SET_BAUD_RATE_ONLY));
  ASSERT_EQ(device_state().DataBits(), SERIAL_DATA_BITS_6);
  ASSERT_EQ(device_state().StopBits(), SERIAL_STOP_BITS_2);
  ASSERT_EQ(device_state().Parity(), SERIAL_PARITY_EVEN);
  ASSERT_TRUE(device_state().FlowControl());

  ASSERT_NOT_OK(Device().SerialImplAsyncConfig(0, serial_test_config));
  ASSERT_NOT_OK(Device().SerialImplAsyncConfig(UINT32_MAX, serial_test_config));
  ASSERT_NOT_OK(Device().SerialImplAsyncConfig(1, serial_test_config));
  ASSERT_EQ(device_state().DataBits(), SERIAL_DATA_BITS_6);
  ASSERT_EQ(device_state().StopBits(), SERIAL_STOP_BITS_2);
  ASSERT_EQ(device_state().Parity(), SERIAL_PARITY_EVEN);
  ASSERT_TRUE(device_state().FlowControl());
  ASSERT_OK(Device().SerialImplAsyncConfig(40, SERIAL_SET_BAUD_RATE_ONLY));
  ASSERT_EQ(device_state().DataBits(), SERIAL_DATA_BITS_6);
  ASSERT_EQ(device_state().StopBits(), SERIAL_STOP_BITS_2);
  ASSERT_EQ(device_state().Parity(), SERIAL_PARITY_EVEN);
  ASSERT_TRUE(device_state().FlowControl());
}

TEST_F(AmlUartHarness, SerialImplAsyncEnable) {
  ASSERT_OK(Device().SerialImplAsyncEnable(false));
  ASSERT_EQ(device_state().Control().tx_enable(), 0);
  ASSERT_EQ(device_state().Control().rx_enable(), 0);
  ASSERT_EQ(device_state().Control().inv_cts(), 0);
  ASSERT_OK(Device().SerialImplAsyncEnable(true));
  ASSERT_EQ(device_state().Control().tx_enable(), 1);
  ASSERT_EQ(device_state().Control().rx_enable(), 1);
  ASSERT_EQ(device_state().Control().inv_cts(), 0);
  ASSERT_TRUE(device_state().PortResetRX());
  ASSERT_TRUE(device_state().PortResetTX());
  ASSERT_FALSE(device_state().Control().rst_rx());
  ASSERT_FALSE(device_state().Control().rst_tx());
  ASSERT_TRUE(device_state().Control().tx_interrupt_enable());
  ASSERT_TRUE(device_state().Control().rx_interrupt_enable());
}

TEST_F(AmlUartHarness, SerialImplReadAsync) {
  ASSERT_OK(Device().SerialImplAsyncEnable(true));
  struct Context {
    uint8_t data[kDataLen];
    sync_completion_t completion;
  } context;
  for (size_t i = 0; i < kDataLen; i++) {
    context.data[i] = static_cast<uint8_t>(i);
  }
  auto cb = [](void* ctx, zx_status_t status, const uint8_t* buffer, size_t bufsz) {
    auto context = static_cast<Context*>(ctx);
    EXPECT_EQ(bufsz, kDataLen);
    EXPECT_EQ(memcmp(buffer, context->data, bufsz), 0);
    sync_completion_signal(&context->completion);
  };
  Device().SerialImplAsyncReadAsync(cb, &context);
  device_state().Inject(context.data, kDataLen);
  sync_completion_wait(&context.completion, ZX_TIME_INFINITE);
}

TEST_F(AmlUartHarness, SerialImplWriteAsync) {
  ASSERT_OK(Device().SerialImplAsyncEnable(true));
  struct Context {
    uint8_t data[kDataLen];
    sync_completion_t completion;
  } context;
  for (size_t i = 0; i < kDataLen; i++) {
    context.data[i] = static_cast<uint8_t>(i);
  }
  auto cb = [](void* ctx, zx_status_t status) {
    auto context = static_cast<Context*>(ctx);
    sync_completion_signal(&context->completion);
  };
  Device().SerialImplAsyncWriteAsync(context.data, kDataLen, cb, &context);
  sync_completion_wait(&context.completion, ZX_TIME_INFINITE);
  auto buf = device_state().TxBuf();
  ASSERT_EQ(buf.size(), kDataLen);
  ASSERT_EQ(memcmp(buf.data(), context.data, buf.size()), 0);
}

TEST_F(AmlUartHarness, SerialImplAsyncWriteDoubleCallback) {
  // NOTE: we don't start the IRQ thread.  The Handle*RaceForTest() enable.
  struct Context {
    uint8_t data[kDataLen];
    sync_completion_t completion;
  } context;
  for (size_t i = 0; i < kDataLen; i++) {
    context.data[i] = static_cast<uint8_t>(i);
  }
  auto cb = [](void* ctx, zx_status_t status) {
    auto context = static_cast<Context*>(ctx);
    sync_completion_signal(&context->completion);
  };
  Device().SerialImplAsyncWriteAsync(context.data, kDataLen, cb, &context);
  Device().HandleTXRaceForTest();
  sync_completion_wait(&context.completion, ZX_TIME_INFINITE);
  auto buf = device_state().TxBuf();
  ASSERT_EQ(buf.size(), kDataLen);
  ASSERT_EQ(memcmp(buf.data(), context.data, buf.size()), 0);
}

TEST_F(AmlUartHarness, SerialImplAsyncReadDoubleCallback) {
  // NOTE: we don't start the IRQ thread.  The Handle*RaceForTest() enable.
  struct Context {
    uint8_t data[kDataLen];
    sync_completion_t completion;
  } context;
  for (size_t i = 0; i < kDataLen; i++) {
    context.data[i] = static_cast<uint8_t>(i);
  }
  auto cb = [](void* ctx, zx_status_t status, const uint8_t* buffer, size_t bufsz) {
    auto context = static_cast<Context*>(ctx);
    EXPECT_EQ(bufsz, kDataLen);
    EXPECT_EQ(memcmp(buffer, context->data, bufsz), 0);
    sync_completion_signal(&context->completion);
  };
  Device().SerialImplAsyncReadAsync(cb, &context);
  device_state().Inject(context.data, kDataLen);
  Device().HandleRXRaceForTest();
  sync_completion_wait(&context.completion, ZX_TIME_INFINITE);
}

}  // namespace
