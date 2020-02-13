// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "uart16550.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-function/mock-function.h>
#include <lib/zx/event.h>

#include <ddk/protocol/serial.h>
#include <ddk/protocol/serialimpl.h>
#include <zxtest/zxtest.h>

namespace {

class Uart16550Harness : public zxtest::Test {
 public:
  void SetUp() override {
    zx::interrupt interrupt;
    ASSERT_OK(zx::interrupt::create({}, 0, ZX_INTERRUPT_VIRTUAL, &interrupt));
    ASSERT_OK(zx::event::create(0, &callback_finished_));

    write_
        .ExpectCall(0b1000'0000, 3)   // divisor latch enable
        .ExpectCall(0b1110'0111, 2)   // fifo control reset
        .ExpectCall(0b0000'0000, 3)   // divisor latch disable
        .ExpectCall(0b1000'0000, 3)   // divisor latch enable
        .ExpectCall(0b0000'0001, 0)   // lower
        .ExpectCall(0b0000'0000, 1)   // upper
        .ExpectCall(0b0000'0011, 3)   // 8N1
        .ExpectCall(0b0000'1011, 4)   // no flow control
        .ExpectCall(0b1000'0000, 3)   // divisor latch enable
        .ExpectCall(0b1110'0111, 2)   // fifo control reset
        .ExpectCall(0b0000'0000, 3)   // divisor latch disable
        .ExpectCall(0b0000'1101, 1);  // enable interrupts

    read_
        .ExpectCall(0b1110'0000, 2)   // interrupt identify
        .ExpectCall(0b0000'0000, 3);  // line control

    auto read_call = [&](uint16_t port) { return read_.Call(port); };

    auto write_call = [&](uint8_t data, uint16_t port) { write_.Call(data, port); };

    device_ = std::make_unique<uart16550::Uart16550>();
    ASSERT_OK(device_->Init(std::move(interrupt), read_call, write_call));
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

    read_.VerifyAndClear();
    write_.VerifyAndClear();
  }

  void TearDown() override {
    if (device_->Enabled()) {
      write_.ExpectCall(0b0000'0000, 1);  // disable interrupts
    } else {
      write_.ExpectNoCall();
    }
    read_.ExpectNoCall();

    uart16550::Uart16550* device = device_.release();
    device->DdkRelease();

    read_.VerifyAndClear();
    write_.VerifyAndClear();
    callback_finished_.reset();
  }

  uart16550::Uart16550& Device() { return *device_; }

  void InterruptDriver() { ASSERT_OK(Device().InterruptHandle()->trigger(0, zx::time())); }

  void SignalCallbackFinished() {
    ASSERT_OK(callback_finished_.signal(ZX_EVENT_SIGNAL_MASK, ZX_EVENT_SIGNALED));
  }

  void WaitCallbackFinished() {
    ASSERT_OK(callback_finished_.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), nullptr));
  }

  mock_function::MockFunction<uint8_t, uint16_t>& ReadMock() { return read_; }

  mock_function::MockFunction<void, uint8_t, uint16_t>& WriteMock() { return write_; }

 private:
  std::unique_ptr<uart16550::Uart16550> device_;
  mock_function::MockFunction<uint8_t, uint16_t> read_;
  mock_function::MockFunction<void, uint8_t, uint16_t> write_;
  zx::event callback_finished_;
};

TEST_F(Uart16550Harness, SerialImplGetInfo) {
  WriteMock().ExpectNoCall();
  ReadMock().ExpectNoCall();

  serial_port_info_t info;
  ASSERT_OK(Device().SerialImplGetInfo(&info));

  WriteMock().VerifyAndClear();
  ReadMock().VerifyAndClear();
}

TEST_F(Uart16550Harness, SerialImplConfig) {
  WriteMock()
      .ExpectCall(0b0000'0000, 1)   // disable interrupts
      .ExpectCall(0b1000'0000, 3)   // enable divisor latch
      .ExpectCall(0b1000'0000, 0)   // lower
      .ExpectCall(0b0001'0110, 1)   // upper
      .ExpectCall(0b0001'1101, 3)   // 6E2
      .ExpectCall(0b0010'1000, 4)   // automatic flow control
      .ExpectCall(0b1001'1101, 3)   // enable divisor latch
      .ExpectCall(0b0100'0000, 0)   // lower
      .ExpectCall(0b0000'1011, 1)   // upper
      .ExpectCall(0b0001'1101, 3);  // disable divisor latch

  ReadMock()
      .ExpectCall(0b0000'0000, 3)   // line control
      .ExpectCall(0b0001'1101, 3);  // line control

  ASSERT_OK(Device().SerialImplEnable(false));

  static constexpr uint32_t serial_test_config =
      SERIAL_DATA_BITS_6 | SERIAL_STOP_BITS_2 | SERIAL_PARITY_EVEN | SERIAL_FLOW_CTRL_CTS_RTS;
  ASSERT_OK(Device().SerialImplConfig(20, serial_test_config));
  ASSERT_OK(Device().SerialImplConfig(40, SERIAL_SET_BAUD_RATE_ONLY));

  ASSERT_NOT_OK(Device().SerialImplConfig(0, serial_test_config));
  ASSERT_NOT_OK(Device().SerialImplConfig(UINT32_MAX, serial_test_config));
  ASSERT_NOT_OK(Device().SerialImplConfig(1, serial_test_config));

  WriteMock().VerifyAndClear();
  ReadMock().VerifyAndClear();
}

TEST_F(Uart16550Harness, SerialImplEnable) {
  WriteMock()
      .ExpectCall(0b0000'0000, 1)   // disable interrupts
      .ExpectCall(0b1000'0000, 3)   // divisor latch enable
      .ExpectCall(0b1110'0111, 2)   // fifo control reset
      .ExpectCall(0b0000'0000, 3)   // divisor latch disable
      .ExpectCall(0b0000'1101, 1);  // enable interrupts

  ReadMock().ExpectNoCall();

  ASSERT_OK(Device().SerialImplEnable(false));
  ASSERT_FALSE(Device().Enabled());
  ASSERT_TRUE(Device().NotifyCallbackSet());

  ASSERT_OK(Device().SerialImplEnable(false));
  ASSERT_FALSE(Device().Enabled());
  ASSERT_TRUE(Device().NotifyCallbackSet());

  ASSERT_OK(Device().SerialImplEnable(true));
  ASSERT_TRUE(Device().Enabled());
  ASSERT_TRUE(Device().NotifyCallbackSet());

  WriteMock().VerifyAndClear();
  ReadMock().VerifyAndClear();
}

TEST_F(Uart16550Harness, SerialImplSetNotifyCallback) {
  WriteMock().ExpectCall(0b0000'0000, 1);  // disable interrupts
  ReadMock().ExpectNoCall();

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

  WriteMock().VerifyAndClear();
  ReadMock().VerifyAndClear();
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

  WriteMock()
      .ExpectCall(0b0000'0000, 1)   // disable interrupts
      .ExpectCall(0b1000'0000, 3)   // divisor latch enable
      .ExpectCall(0b1110'0111, 2)   // fifo control reset
      .ExpectCall(0b0000'0000, 3)   // divisor latch disable
      .ExpectCall(0b0000'1101, 1);  // enable interrupts

  ReadMock()
      .ExpectCall(0b0000'0000, 5)   // data not ready
      .ExpectCall(0b0000'0100, 2)   // rx available interrupt id
      .ExpectCall(0b0000'0001, 5)   // data ready
      .ExpectCall(0b0000'0001, 5)   // data ready
      .ExpectCall(0x0F, 0)          // buffer[0]
      .ExpectCall(0b0000'0001, 5)   // data ready
      .ExpectCall(0xF0, 0)          // buffer[1]
      .ExpectCall(0b0000'0001, 5)   // data ready
      .ExpectCall(0x59, 0)          // buffer[2]
      .ExpectCall(0b0000'0000, 5);  // data ready

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

  WriteMock().VerifyAndClear();
  ReadMock().VerifyAndClear();
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

  WriteMock()
      .ExpectCall(0b0000'0000, 1)  // disable interrupts
      .ExpectCall(0b1000'0000, 3)  // divisor latch enable
      .ExpectCall(0b1110'0111, 2)  // fifo control reset
      .ExpectCall(0b0000'0000, 3)  // divisor latch disable
      .ExpectCall(0b0000'1101, 1)  // enable interrupts
      .ExpectCall(0b0000'1111, 1)  // write interrupts
      .ExpectCall(0b0000'1101, 1)  // write interrupts
      .ExpectCall(0xDE, 0)         // writable_buffer[0]
      .ExpectCall(0xAD, 0)         // writable_buffer[1]
      .ExpectCall(0xBE, 0)         // writable_buffer[2]
      .ExpectCall(0xEF, 0);        // writable_buffer[3]

  ReadMock()
      .ExpectCall(0b0000'0000, 5)   // tx empty
      .ExpectCall(0b0000'1101, 1)   // read interrupts
      .ExpectCall(0b0000'0010, 2)   // tx empty interrupt id
      .ExpectCall(0b0000'1111, 1)   // read interrupts
      .ExpectCall(0b0100'0000, 5);  // tx empty

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

  WriteMock().VerifyAndClear();
  ReadMock().VerifyAndClear();
}

}  // namespace
