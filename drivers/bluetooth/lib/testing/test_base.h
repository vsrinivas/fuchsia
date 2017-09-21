// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/drivers/bluetooth/lib/hci/acl_data_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/acl_data_packet.h"
#include "garnet/drivers/bluetooth/lib/hci/device_wrapper.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

// This file provides a basic set of GTest unit test harnesses that performs
// common set-up/tear-down operations, including setting up a message loop,
// creating a stub HCI controller, etc.

namespace bluetooth {
namespace testing {

class FakeControllerBase;

// GTest Test harness derivative. GTest uses the top-level "testing" namespace
// while the Bluetooth test utilities are in "bluetooth::testing".
template <class FakeControllerType>
class TestBase : public ::testing::Test {
 public:
  TestBase() = default;
  virtual ~TestBase() = default;

 protected:
  // Pure-virtual overrides of ::testing::Test::SetUp() to force subclass
  // override.
  void SetUp() override = 0;

  // ::testing::Test override:
  void TearDown() override { test_device_ = nullptr; }

  // Initializes |test_device_| and returns the DeviceWrapper endpoint which can
  // be passed to classes that are under test.
  std::unique_ptr<hci::DeviceWrapper> SetUpTestDevice() {
    zx::channel cmd0, cmd1;
    zx::channel acl0, acl1;

    zx_status_t status = zx::channel::create(0, &cmd0, &cmd1);
    FXL_DCHECK(ZX_OK == status);

    status = zx::channel::create(0, &acl0, &acl1);
    FXL_DCHECK(ZX_OK == status);

    auto hci_dev = std::make_unique<hci::DummyDeviceWrapper>(std::move(cmd0),
                                                             std::move(acl0));
    test_device_ =
        std::make_unique<FakeControllerType>(std::move(cmd1), std::move(acl1));

    return hci_dev;
  }

  // Posts a delayed task to quit the message loop after |seconds| have elapsed.
  void PostDelayedQuitTask(const fxl::TimeDelta time_delta) {
    message_loop_.task_runner()->PostDelayedTask(
        [this] { message_loop_.QuitNow(); }, time_delta);
  }

  // Runs the message loop for the specified amount of time. This is useful for
  // callback-driven test cases in which the message loop may run forever if the
  // callback is not run.
  void RunMessageLoop(int64_t timeout_seconds = 10) {
    RunMessageLoop(fxl::TimeDelta::FromSeconds(timeout_seconds));
  }

  void RunMessageLoop(const fxl::TimeDelta& time_delta) {
    PostDelayedQuitTask(time_delta);
    message_loop_.Run();
  }

  // Deletes |test_device_| and resets the pointer.
  void DeleteTestDevice() { test_device_ = nullptr; }

  // Getters for internal fields frequently used by tests.
  FakeControllerType* test_device() const { return test_device_.get(); }
  fsl::MessageLoop* message_loop() { return &message_loop_; }

 private:
  std::unique_ptr<FakeControllerType> test_device_;
  fsl::MessageLoop message_loop_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestBase);
  static_assert(
      std::is_base_of<FakeControllerBase, FakeControllerType>::value,
      "TestBase must be used with a derivative of FakeControllerBase");
};

// This harness sets up a HCI Transport for transport-level tests.
template <class FakeControllerType>
class TransportTest : public TestBase<FakeControllerType> {
 public:
  TransportTest() = default;
  virtual ~TransportTest() = default;

 protected:
  // TestBase overrides:
  void SetUp() override {
    transport_ =
        hci::Transport::Create(TestBase<FakeControllerType>::SetUpTestDevice());
    transport_->Initialize();
  }

  void TearDown() override {
    transport_ = nullptr;
    TestBase<FakeControllerType>::TearDown();
  }

  bool InitializeACLDataChannel(const hci::DataBufferInfo& bredr_buffer_info,
                                const hci::DataBufferInfo& le_buffer_info) {
    if (!transport_->InitializeACLDataChannel(bredr_buffer_info,
                                              le_buffer_info)) {
      return false;
    }

    transport_->acl_data_channel()->SetDataRxHandler(
        std::bind(&TransportTest<FakeControllerType>::OnDataReceived, this,
                  std::placeholders::_1));

    return true;
  }

  // Sets a callback which will be invoked when we receive packets from the test
  // controller. |callback| will be posted on the test main loop (i.e.
  // TestBase::message_loop_), thus no locking is necessary within the callback.
  void set_data_received_callback(
      const hci::ACLDataChannel::DataReceivedCallback& callback) {
    data_received_callback_ = callback;
  }

  fxl::RefPtr<hci::Transport> transport() const { return transport_; }
  hci::CommandChannel* cmd_channel() const {
    return transport_->command_channel();
  }
  hci::ACLDataChannel* acl_data_channel() const {
    return transport_->acl_data_channel();
  }

 private:
  void OnDataReceived(hci::ACLDataPacketPtr data_packet) {
    // Accessing |data_received_callback_| is racy but unlikely to cause issues
    // in unit tests. NOTE(armansito): Famous last words?
    if (!data_received_callback_)
      return;

    TestBase<FakeControllerType>::message_loop()->task_runner()->PostTask(
        fxl::MakeCopyable([ this, packet = std::move(data_packet) ]() mutable {
          data_received_callback_(std::move(packet));
        }));
  }

  hci::ACLDataChannel::DataReceivedCallback data_received_callback_;
  fxl::RefPtr<hci::Transport> transport_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TransportTest);
};

}  // namespace testing
}  // namespace bluetooth
