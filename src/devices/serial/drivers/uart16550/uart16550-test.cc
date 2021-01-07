// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "uart16550.h"

#include <fuchsia/hardware/serial/c/banjo.h>
#include <fuchsia/hardware/serialimpl/c/banjo.h>
#include <lib/zx/event.h>

#include <zxtest/zxtest.h>

namespace {

class Uart16550Harness : public zxtest::Test {
 public:
  void SetUp() override {
    zx::interrupt interrupt;
    ASSERT_OK(zx::interrupt::create({}, 0, ZX_INTERRUPT_VIRTUAL, &interrupt));
    ASSERT_OK(zx::event::create(0, &callback_finished_));

    port_mock_
        .ExpectWrite<uint8_t>(0b1000'0000, 3)   // divisor latch enable
        .ExpectWrite<uint8_t>(0b1110'0111, 2)   // fifo control reset
        .ExpectWrite<uint8_t>(0b0000'0000, 3)   // divisor latch disable
        .ExpectRead<uint8_t>(0b1110'0000, 2)    // interrupt identify
        .ExpectRead<uint8_t>(0b0000'0000, 3)    // line control
        .ExpectWrite<uint8_t>(0b1000'0000, 3)   // divisor latch enable
        .ExpectWrite<uint8_t>(0b0000'0001, 0)   // lower
        .ExpectWrite<uint8_t>(0b0000'0000, 1)   // upper
        .ExpectWrite<uint8_t>(0b0000'0011, 3)   // 8N1
        .ExpectWrite<uint8_t>(0b0000'1011, 4)   // no flow control
        .ExpectWrite<uint8_t>(0b1000'0000, 3)   // divisor latch enable
        .ExpectWrite<uint8_t>(0b1110'0111, 2)   // fifo control reset
        .ExpectWrite<uint8_t>(0b0000'0000, 3)   // divisor latch disable
        .ExpectWrite<uint8_t>(0b0000'1101, 1);  // enable interrupts

    device_ = std::make_unique<uart16550::Uart16550>();
    ASSERT_OK(device_->Init(std::move(interrupt), *port_mock_.io()));
    ASSERT_EQ(device_->FifoDepth(), 64);
    ASSERT_FALSE(device_->Enabled());
    ASSERT_FALSE(device_->NotifyCallbackSet());

    serial_notify_t notify = {
        .callback = [](void* /*unused*/, serial_state_t /*unused*/) { ASSERT_TRUE(false); },
        .ctx = nullptr,
    };

    ASSERT_OK(device_->SerialImplSetNotifyCallback(&notify));
    ASSERT_FALSE(device_->Enabled());
    ASSERT_TRUE(device_->NotifyCallbackSet());

    ASSERT_OK(device_->SerialImplEnable(true));
    ASSERT_TRUE(device_->Enabled());
    ASSERT_TRUE(device_->NotifyCallbackSet());

    port_mock_.VerifyAndClear();
  }

  void TearDown() override {
    if (device_->Enabled()) {
      port_mock_.ExpectWrite<uint8_t>(0b0000'0000, 1);  // disable interrupts
    } else {
      port_mock_.ExpectNoIo();
    }

    uart16550::Uart16550* device = device_.release();
    device->DdkRelease();

    port_mock_.ExpectNoIo();
    callback_finished_.reset();
  }

  uart16550::Uart16550& Device() { return *device_; }

  hwreg::Mock& PortMock() { return port_mock_; }

  void InterruptDriver() { ASSERT_OK(Device().InterruptHandle()->trigger(0, zx::time())); }

  void SignalCallbackFinished() {
    ASSERT_OK(callback_finished_.signal(ZX_EVENT_SIGNAL_MASK, ZX_EVENT_SIGNALED));
  }

  void WaitCallbackFinished() {
    ASSERT_OK(callback_finished_.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), nullptr));
  }

 private:
  std::unique_ptr<uart16550::Uart16550> device_;
  hwreg::Mock port_mock_;
  zx::event callback_finished_;
};

TEST_F(Uart16550Harness, SerialImplGetInfo) {
  PortMock().ExpectNoIo();

  serial_port_info_t info;
  ASSERT_OK(Device().SerialImplGetInfo(&info));

  PortMock().VerifyAndClear();
}

TEST_F(Uart16550Harness, SerialImplConfig) {
  PortMock()
      .ExpectWrite<uint8_t>(0b0000'0000, 1)   // disable interrupts
      .ExpectRead<uint8_t>(0b0000'0000, 3)    // line control
      .ExpectWrite<uint8_t>(0b1000'0000, 3)   // enable divisor latch
      .ExpectWrite<uint8_t>(0b1000'0000, 0)   // lower
      .ExpectWrite<uint8_t>(0b0001'0110, 1)   // upper
      .ExpectWrite<uint8_t>(0b0001'1101, 3)   // 6E2
      .ExpectWrite<uint8_t>(0b0010'1000, 4)   // automatic flow control
      .ExpectRead<uint8_t>(0b0001'1101, 3)    // line control
      .ExpectWrite<uint8_t>(0b1001'1101, 3)   // enable divisor latch
      .ExpectWrite<uint8_t>(0b0100'0000, 0)   // lower
      .ExpectWrite<uint8_t>(0b0000'1011, 1)   // upper
      .ExpectWrite<uint8_t>(0b0001'1101, 3);  // disable divisor latch

  ASSERT_OK(Device().SerialImplEnable(false));

  static constexpr uint32_t serial_test_config =
      SERIAL_DATA_BITS_6 | SERIAL_STOP_BITS_2 | SERIAL_PARITY_EVEN | SERIAL_FLOW_CTRL_CTS_RTS;
  ASSERT_OK(Device().SerialImplConfig(20, serial_test_config));
  ASSERT_OK(Device().SerialImplConfig(40, SERIAL_SET_BAUD_RATE_ONLY));

  ASSERT_NOT_OK(Device().SerialImplConfig(0, serial_test_config));
  ASSERT_NOT_OK(Device().SerialImplConfig(UINT32_MAX, serial_test_config));
  ASSERT_NOT_OK(Device().SerialImplConfig(1, serial_test_config));

  PortMock().VerifyAndClear();
}

TEST_F(Uart16550Harness, SerialImplEnable) {
  PortMock()
      .ExpectWrite<uint8_t>(0b0000'0000, 1)   // disable interrupts
      .ExpectWrite<uint8_t>(0b1000'0000, 3)   // divisor latch enable
      .ExpectWrite<uint8_t>(0b1110'0111, 2)   // fifo control reset
      .ExpectWrite<uint8_t>(0b0000'0000, 3)   // divisor latch disable
      .ExpectWrite<uint8_t>(0b0000'1101, 1);  // enable interrupts

  ASSERT_OK(Device().SerialImplEnable(false));
  ASSERT_FALSE(Device().Enabled());
  ASSERT_TRUE(Device().NotifyCallbackSet());

  ASSERT_OK(Device().SerialImplEnable(false));
  ASSERT_FALSE(Device().Enabled());
  ASSERT_TRUE(Device().NotifyCallbackSet());

  ASSERT_OK(Device().SerialImplEnable(true));
  ASSERT_TRUE(Device().Enabled());
  ASSERT_TRUE(Device().NotifyCallbackSet());

  PortMock().VerifyAndClear();
}

TEST_F(Uart16550Harness, SerialImplSetNotifyCallback) {
  PortMock().ExpectWrite<uint8_t>(0b0000'0000, 1);  // disable interrupts

  serial_notify_t notify = {
      .callback = [](void* /*unused*/, serial_state_t /*unused*/) { ASSERT_TRUE(false); },
      .ctx = nullptr,
  };

  ASSERT_NOT_OK(Device().SerialImplSetNotifyCallback(&notify));
  ASSERT_TRUE(Device().Enabled());
  ASSERT_TRUE(Device().NotifyCallbackSet());

  ASSERT_OK(Device().SerialImplEnable(false));
  ASSERT_OK(Device().SerialImplSetNotifyCallback(&notify));
  ASSERT_TRUE(Device().NotifyCallbackSet());

  ASSERT_OK(Device().SerialImplSetNotifyCallback(nullptr));
  ASSERT_FALSE(Device().NotifyCallbackSet());

  ASSERT_OK(Device().SerialImplSetNotifyCallback(&notify));
  ASSERT_TRUE(Device().NotifyCallbackSet());

  notify.callback = nullptr;

  ASSERT_TRUE(Device().NotifyCallbackSet());

  ASSERT_OK(Device().SerialImplSetNotifyCallback(&notify));
  ASSERT_FALSE(Device().NotifyCallbackSet());

  PortMock().VerifyAndClear();
}

TEST_F(Uart16550Harness, SerialImplRead) {
  auto readable = [](void* ctx, serial_state_t state) {
    auto* harness = static_cast<Uart16550Harness*>(ctx);
    if (state & SERIAL_STATE_READABLE) {
      harness->SignalCallbackFinished();
    }
  };

  serial_notify_t notify = {
      .callback = readable,
      .ctx = this,
  };

  PortMock()
      .ExpectWrite<uint8_t>(0b0000'0000, 1)  // disable interrupts
      .ExpectWrite<uint8_t>(0b1000'0000, 3)  // divisor latch enable
      .ExpectWrite<uint8_t>(0b1110'0111, 2)  // fifo control reset
      .ExpectWrite<uint8_t>(0b0000'0000, 3)  // divisor latch disable
      .ExpectWrite<uint8_t>(0b0000'1101, 1)  // enable interrupts
      .ExpectRead<uint8_t>(0b0000'0000, 5)   // data not ready
      .ExpectRead<uint8_t>(0b0000'0100, 2)   // rx available interrupt id
      .ExpectRead<uint8_t>(0b0000'0001, 5)   // data ready
      .ExpectRead<uint8_t>(0b0000'0001, 5)   // data ready
      .ExpectRead<uint8_t>(0x0F, 0)          // buffer[0]
      .ExpectRead<uint8_t>(0b0000'0001, 5)   // data ready
      .ExpectRead<uint8_t>(0xF0, 0)          // buffer[1]
      .ExpectRead<uint8_t>(0b0000'0001, 5)   // data ready
      .ExpectRead<uint8_t>(0x59, 0)          // buffer[2]
      .ExpectRead<uint8_t>(0b0000'0000, 5);  // data ready

  ASSERT_OK(Device().SerialImplEnable(false));
  ASSERT_OK(Device().SerialImplSetNotifyCallback(&notify));

  uint8_t unreadable_buffer[1] = {0};
  size_t actual;

  ASSERT_EQ(Device().SerialImplRead(unreadable_buffer, sizeof(unreadable_buffer), &actual),
            ZX_ERR_BAD_STATE);
  ASSERT_EQ(actual, 0);

  ASSERT_OK(Device().SerialImplEnable(true));

  ASSERT_EQ(Device().SerialImplRead(unreadable_buffer, sizeof(unreadable_buffer), &actual),
            ZX_ERR_SHOULD_WAIT);
  ASSERT_EQ(actual, 0);

  InterruptDriver();
  WaitCallbackFinished();

  uint8_t readable_buffer[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  uint8_t expect_buffer[4] = {0x0F, 0xF0, 0x59, 0xEF};

  ASSERT_OK(Device().SerialImplRead(readable_buffer, sizeof(readable_buffer), &actual));
  ASSERT_EQ(actual, 3);
  ASSERT_BYTES_EQ(readable_buffer, expect_buffer, sizeof(readable_buffer));

  PortMock().VerifyAndClear();
}

TEST_F(Uart16550Harness, SerialImplWrite) {
  auto writable = [](void* ctx, serial_state_t state) {
    auto* harness = static_cast<Uart16550Harness*>(ctx);
    if (state & SERIAL_STATE_WRITABLE) {
      harness->SignalCallbackFinished();
    }
  };

  serial_notify_t notify = {
      .callback = writable,
      .ctx = this,
  };

  PortMock()
      .ExpectWrite<uint8_t>(0b0000'0000, 1)  // disable interrupts
      .ExpectWrite<uint8_t>(0b1000'0000, 3)  // divisor latch enable
      .ExpectWrite<uint8_t>(0b1110'0111, 2)  // fifo control reset
      .ExpectWrite<uint8_t>(0b0000'0000, 3)  // divisor latch disable
      .ExpectWrite<uint8_t>(0b0000'1101, 1)  // enable interrupts
      .ExpectRead<uint8_t>(0b0000'0000, 5)   // tx empty
      .ExpectRead<uint8_t>(0b0000'1101, 1)   // read interrupts
      .ExpectWrite<uint8_t>(0b0000'1111, 1)  // write interrupts
      .ExpectRead<uint8_t>(0b0000'0010, 2)   // tx empty interrupt id
      .ExpectRead<uint8_t>(0b0000'1111, 1)   // read interrupts
      .ExpectWrite<uint8_t>(0b0000'1101, 1)  // write interrupts
      .ExpectRead<uint8_t>(0b0100'0000, 5)   // tx empty
      .ExpectWrite<uint8_t>(0xDE, 0)         // writable_buffer[0]
      .ExpectWrite<uint8_t>(0xAD, 0)         // writable_buffer[1]
      .ExpectWrite<uint8_t>(0xBE, 0)         // writable_buffer[2]
      .ExpectWrite<uint8_t>(0xEF, 0);        // writable_buffer[3]

  ASSERT_OK(Device().SerialImplEnable(false));
  ASSERT_OK(Device().SerialImplSetNotifyCallback(&notify));

  uint8_t unwritable_buffer[1] = {0};
  size_t actual;

  ASSERT_EQ(Device().SerialImplWrite(unwritable_buffer, sizeof(unwritable_buffer), &actual),
            ZX_ERR_BAD_STATE);
  ASSERT_EQ(actual, 0);

  ASSERT_OK(Device().SerialImplEnable(true));

  ASSERT_EQ(Device().SerialImplWrite(unwritable_buffer, sizeof(unwritable_buffer), &actual),
            ZX_ERR_SHOULD_WAIT);
  ASSERT_EQ(actual, 0);

  InterruptDriver();
  WaitCallbackFinished();

  uint8_t writable_buffer[4] = {0xDE, 0xAD, 0xBE, 0xEF};

  ASSERT_OK(Device().SerialImplWrite(writable_buffer, sizeof(writable_buffer), &actual));
  ASSERT_EQ(actual, 4);

  PortMock().VerifyAndClear();
}

}  // namespace
