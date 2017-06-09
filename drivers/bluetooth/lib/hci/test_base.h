// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/bluetooth/lib/hci/acl_data_channel.h"
#include "apps/bluetooth/lib/hci/device_wrapper.h"
#include "apps/bluetooth/lib/hci/transport.h"
#include "gtest/gtest.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

// This file provides a basic set of GTest unit test harnesses that performs common set-up/tear-down
// operations, including setting up a message loop, creating a stub HCI controller, etc.

namespace bluetooth {
namespace hci {

class DeviceWrapper;

namespace test {

class FakeControllerBase;

template <class FakeControllerType>
class TestBase : public ::testing::Test {
 public:
  TestBase() = default;
  virtual ~TestBase() = default;

 protected:
  // Pure-virtual overrides of ::testing::Test::SetUp() to force subclass override.
  void SetUp() override = 0;

  // ::testing::Test override:
  void TearDown() override { test_device_ = nullptr; }

  // Initializes |test_device_| and returns the DeviceWrapper endpoint which can be passed to
  // classes that are under test.
  std::unique_ptr<DeviceWrapper> SetUpTestDevice() {
    mx::channel cmd0, cmd1;
    mx::channel acl0, acl1;

    mx_status_t status = mx::channel::create(0, &cmd0, &cmd1);
    FTL_DCHECK(MX_OK == status);

    status = mx::channel::create(0, &acl0, &acl1);
    FTL_DCHECK(MX_OK == status);

    auto hci_dev = std::make_unique<DummyDeviceWrapper>(std::move(cmd0), std::move(acl0));
    test_device_ = std::make_unique<FakeControllerType>(std::move(cmd1), std::move(acl1));

    return hci_dev;
  }

  // Posts a delayed task to quit the message loop after |seconds| have elapsed.
  void PostDelayedQuitTask(int64_t seconds) {
    message_loop_.task_runner()->PostDelayedTask([this] { message_loop_.QuitNow(); },
                                                 ftl::TimeDelta::FromSeconds(seconds));
  }

  // Runs the message loop for the specified amount of time. This is useful for callback-driven test
  // cases in which the message loop may run forever if the callback is not run.
  void RunMessageLoop(int64_t timeout_seconds = 10) {
    PostDelayedQuitTask(timeout_seconds);
    message_loop_.Run();
  }

  // Deletes |test_device_| and resets the pointer.
  void DeleteTestDevice() { test_device_ = nullptr; }

  // Getters for internal fields frequently used by tests.
  FakeControllerType* test_device() const { return test_device_.get(); }
  mtl::MessageLoop* message_loop() { return &message_loop_; }

 private:
  std::unique_ptr<FakeControllerType> test_device_;
  mtl::MessageLoop message_loop_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TestBase);
  static_assert(std::is_base_of<FakeControllerBase, FakeControllerType>::value,
                "TestBase must be used with a derivative of FakeControllerBase");
};

// This harness sets up a HCI Transport for transport-level tests.
template <class FakeControllerType>
class TransportTest : public TestBase<FakeControllerType> {
 public:
  TransportTest() = default;
  virtual ~TransportTest() = default;

 protected:
  // ::test::Test overrides:
  void SetUp() override {
    transport_ = Transport::Create(TestBase<FakeControllerType>::SetUpTestDevice());
    transport_->Initialize();
  }

  void TearDown() override {
    transport_ = nullptr;
    TestBase<FakeControllerType>::TearDown();
  }

  bool InitializeACLDataChannel(const DataBufferInfo& bredr_buffer_info,
                                const DataBufferInfo& le_buffer_info) {
    if (!transport_->InitializeACLDataChannel(
            bredr_buffer_info, le_buffer_info,
            std::bind(&TransportTest<FakeControllerType>::LookUpConnection, this,
                      std::placeholders::_1))) {
      return false;
    }

    transport_->acl_data_channel()->SetDataRxHandler(
        std::bind(&TransportTest<FakeControllerType>::OnDataReceived, this, std::placeholders::_1),
        TestBase<FakeControllerType>::message_loop()->task_runner());

    return true;
  }

  void set_data_received_callback(const ACLDataChannel::DataReceivedCallback& callback) {
    data_received_callback_ = callback;
  }

  void set_connection_lookup_callback(const ACLDataChannel::ConnectionLookupCallback& callback) {
    connection_lookup_callback_ = callback;
  }

  ftl::RefPtr<Transport> transport() const { return transport_; }
  CommandChannel* cmd_channel() const { return transport_->command_channel(); }
  ACLDataChannel* acl_data_channel() const { return transport_->acl_data_channel(); }

 private:
  void OnDataReceived(common::DynamicByteBuffer acl_data_bytes) {
    if (data_received_callback_) data_received_callback_(std::move(acl_data_bytes));
  }

  ftl::RefPtr<Connection> LookUpConnection(ConnectionHandle handle) {
    if (!connection_lookup_callback_) return nullptr;
    return connection_lookup_callback_(handle);
  }

  ACLDataChannel::DataReceivedCallback data_received_callback_;
  ACLDataChannel::ConnectionLookupCallback connection_lookup_callback_;
  ftl::RefPtr<Transport> transport_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TransportTest);
};

}  // namespace test
}  // namespace hci
}  // namespace bluetooth
