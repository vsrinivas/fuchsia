// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <lib/sync/completion.h>
#include <lib/zx/clock.h>
#include <lib/zx/interrupt.h>
#include <zircon/compiler.h>

#include <condition_variable>

#include <hid/boot.h>
#include <hid/usages.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/ui/input/drivers/pc-ps2/commands.h"
#include "src/ui/input/drivers/pc-ps2/controller.h"
#include "src/ui/input/drivers/pc-ps2/device.h"
#include "src/ui/input/drivers/pc-ps2/keymap.h"
#include "src/ui/input/drivers/pc-ps2/registers.h"

class Fake8042;
namespace {
Fake8042* FAKE_INSTANCE = nullptr;
}
class Fake8042 {
 public:
  explicit Fake8042() {
    status_.set_reg_value(0);
    ctrl_.set_reg_value(0);
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &port1_irq_);
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &port2_irq_);
    FAKE_INSTANCE = this;
  }
  ~Fake8042() { FAKE_INSTANCE = nullptr; }
  uint8_t inp(uint16_t port) {
    switch (port) {
      case i8042::kStatusReg:
        return status_.reg_value();
      case i8042::kDataReg: {
        ZX_ASSERT(status_.obf());
        uint8_t value = data_.front();
        data_.pop_front();
        status_.set_obf(!data_.empty());
        return value;
      }
      default:
        ZX_ASSERT_MSG(false, "Unexpected register 0x%x", port);
    }
  }
  void outp(uint16_t port, uint8_t data) {
    switch (port) {
      case i8042::kCommandReg:
        HandleCommand(data);
        break;
      case i8042::kDataReg:
        HandleData(data);
        break;
    }
  }

  void EnablePort2() {
    has_port2_ = true;
    ctrl_.set_auxdis(1);
  }

  void SendDataAndIrq(bool port2, uint8_t byte) {
    SendData(byte);
    if (port2) {
      port2_irq_.trigger(0, zx::clock::get_monotonic());
    } else {
      port1_irq_.trigger(0, zx::clock::get_monotonic());
    }
  }

  void SendData(uint8_t data) {
    data_.emplace_back(data);
    status_.set_obf(1);
  }

  zx::interrupt& port1_irq() { return port1_irq_; }
  zx::interrupt& port2_irq() { return port2_irq_; }

 private:
  enum State {
    // Default state (next write goes to port 1).
    kPort1Write = 0,
    // Next write goes to port 2.
    kPort2Write = 1,
    // Next write goes to control register.
    kControlWrite = 2,
  };

  i8042::StatusReg status_;
  i8042::ControlReg ctrl_;
  State data_state_ = State::kPort1Write;
  bool has_port2_ = false;
  std::list<uint8_t> data_;
  zx::interrupt port1_irq_;
  zx::interrupt port2_irq_;

  void HandleCommand(uint8_t cmd) {
    switch (cmd) {
      case i8042::kCmdReadCtl.cmd:
        SendData(ctrl_.reg_value());
        break;
      case i8042::kCmdWriteCtl.cmd:
        data_state_ = State::kControlWrite;
        break;
      case i8042::kCmdSelfTest.cmd:
        SendData(0x55);
        break;
      case i8042::kCmdWriteAux.cmd:
        ZX_ASSERT(has_port2_);
        data_state_ = State::kPort2Write;
        break;
      case i8042::kCmdPort1Disable.cmd:
      case i8042::kCmdPort1Enable.cmd:
      case i8042::kCmdPort2Disable.cmd:
      case i8042::kCmdPort2Enable.cmd:
        break;

      case i8042::kCmdPort2Test.cmd:
        ZX_ASSERT(has_port2_);
        __FALLTHROUGH;
      case i8042::kCmdPort1Test.cmd:
        SendData(0x00);
        break;
      default:
        ZX_ASSERT_MSG(false, "Unknown command");
    }
  }

  void HandleData(uint8_t data) {
    switch (data_state_) {
      case State::kControlWrite:
        ctrl_.set_reg_value(data);
        break;
      case State::kPort1Write:
        HandleDeviceCommand(false, data);
        break;
      case State::kPort2Write:
        HandleDeviceCommand(true, data);
        break;
    }
    data_state_ = State::kPort1Write;
  }

  void HandleDeviceCommand(bool is_port2, uint8_t command) {
    switch (command) {
      case i8042::kCmdDeviceIdentify.cmd:
        SendData(i8042::kAck);
        SendData(is_port2 ? 0x00 : 0xab);
        break;
      case i8042::kCmdDeviceScanDisable.cmd:
      case i8042::kCmdDeviceScanEnable.cmd:
        SendData(i8042::kAck);
        break;
    }
  }
};

uint8_t TEST_inp(uint16_t port) { return FAKE_INSTANCE->inp(port); }
void TEST_outp(uint16_t port, uint8_t data) { FAKE_INSTANCE->outp(port, data); }
zx::interrupt GetInterrupt(uint32_t irq_no) {
  zx::interrupt* irq = nullptr;
  if (irq_no == 0x1) {
    irq = &FAKE_INSTANCE->port1_irq();
  } else if (irq_no == 0xc) {
    irq = &FAKE_INSTANCE->port2_irq();
  } else {
    ZX_ASSERT_MSG(false, "unexpected irq_no 0x%x", irq_no);
  }

  zx::interrupt clone;
  ZX_ASSERT(irq->duplicate(ZX_RIGHT_SAME_RIGHTS, &clone) == ZX_OK);
  return clone;
}

class ControllerTest : public zxtest::Test, public ddk::HidbusIfcProtocol<ControllerTest> {
 public:
  void SetUp() override {
    root_ = MockDevice::FakeRootParent();
    zx_status_t status = i8042::Controller::Bind(nullptr, root_.get());
    ASSERT_OK(status);

    controller_dev_ = root_->GetLatestChild();
  }

  void TearDown() override {
    device_async_remove(controller_dev_);
    mock_ddk::ReleaseFlaggedDevices(controller_dev_);
  }

  void InitDevices() {
    controller_dev_->InitOp();
    controller_dev_->WaitUntilInitReplyCalled(zx::time::infinite());
    ASSERT_OK(controller_dev_->InitReplyCallStatus());
    sync_completion_wait(&controller_dev_->GetDeviceContext<i8042::Controller>()->added_children(),
                         ZX_TIME_INFINITE);
  }

  void HidbusIfcIoQueue(const uint8_t* buf_buffer, size_t buf_size, zx_time_t timestamp) {
    std::scoped_lock lock(buf_lock_);
    last_buffer_.resize(buf_size);
    memcpy(last_buffer_.data(), buf_buffer, buf_size);
    received_ = true;
    received_event_.notify_all();
  }

  std::vector<uint8_t> WaitForIo() {
    std::scoped_lock lock(buf_lock_);
    received_event_.wait(buf_lock_, [this]() { return received_; });
    received_ = false;
    auto ret = std::move(last_buffer_);
    last_buffer_ = {};
    return ret;
  }

 protected:
  Fake8042 i8042_;
  std::shared_ptr<MockDevice> root_;
  zx_device* controller_dev_;
  std::mutex buf_lock_;
  std::condition_variable_any received_event_;
  std::vector<uint8_t> last_buffer_ __TA_GUARDED(buf_lock_);
  bool received_ = false;

  const hidbus_ifc_protocol_t proto_{
      .ops = &hidbus_ifc_protocol_ops_,
      .ctx = this,
  };
};

TEST_F(ControllerTest, KeyboardPressTest) {
  InitDevices();
  zx_device* dev = controller_dev_->GetLatestChild();
  auto keyboard = dev->GetDeviceContext<i8042::I8042Device>();
  ASSERT_OK(keyboard->HidbusStart(&proto_));
  i8042_.SendDataAndIrq(false, 0x2);

  hid_boot_kbd_report_t report;
  auto buf = WaitForIo();
  memcpy(&report, buf.data(), sizeof(report));
  ASSERT_EQ(report.usage[0], HID_USAGE_KEY_1);

  i8042_.SendDataAndIrq(false, i8042::kKeyUp | 0x2);
  buf = WaitForIo();
  memcpy(&report, buf.data(), sizeof(report));
  ASSERT_EQ(report.usage[0], 0);
}

TEST_F(ControllerTest, MouseMoveTest) {
  i8042_.EnablePort2();
  InitDevices();

  zx_device* dev = controller_dev_->GetLatestChild();
  auto mouse = dev->GetDeviceContext<i8042::I8042Device>();
  ASSERT_OK(mouse->HidbusStart(&proto_));
  i8042_.SendData(0x09 /* button_left | always_one */);
  i8042_.SendData(0x70) /* rel_x */;
  i8042_.SendDataAndIrq(true, 0x10 /* rel_y */);

  hid_boot_mouse_report_t report;
  auto buf = WaitForIo();
  memcpy(&report, buf.data(), sizeof(report));
  ASSERT_EQ(report.buttons, 0x1);
  ASSERT_EQ(report.rel_x, 0x70);
  ASSERT_EQ(report.rel_y, -16);
}
