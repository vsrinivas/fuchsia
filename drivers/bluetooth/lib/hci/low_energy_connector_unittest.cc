// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/hci/low_energy_connector.h"

#include <vector>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/hci/defaults.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_device.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace hci {
namespace {

using ::bluetooth::testing::FakeController;
using ::bluetooth::testing::FakeDevice;
using TestingBase = ::bluetooth::testing::FakeControllerTest<FakeController>;

const common::DeviceAddress kTestAddress(common::DeviceAddress::Type::kLEPublic,
                                         "00:00:00:00:00:01");
constexpr int64_t kTestTimeoutMs = 2000;

class LowEnergyConnectorTest : public TestingBase {
 public:
  LowEnergyConnectorTest() : quit_loop_on_new_connection(false) {}
  ~LowEnergyConnectorTest() override = default;

 protected:
  // TestingBase overrides:
  void SetUp() override {
    TestingBase::SetUp();

    FakeController::Settings settings;
    settings.ApplyLegacyLEConfig();
    test_device()->set_settings(settings);

    connector_ = std::make_unique<LowEnergyConnector>(
        transport(), message_loop()->task_runner(),
        std::bind(&LowEnergyConnectorTest::OnConnectionCreated, this,
                  std::placeholders::_1));

    test_device()->SetConnectionStateCallback(
        std::bind(&LowEnergyConnectorTest::OnConnectionStateChanged, this,
                  std::placeholders::_1, std::placeholders::_2,
                  std::placeholders::_3),
        message_loop()->task_runner());

    test_device()->Start();
  }

  void TearDown() override {
    connector_ = nullptr;
    test_device()->Stop();
    TestingBase::TearDown();
  }

  void DeleteConnector() { connector_ = nullptr; }

  bool request_canceled = false;
  bool quit_loop_on_new_connection = false;
  bool quit_loop_on_cancel = false;

  const std::vector<std::unique_ptr<Connection>>& connections() const {
    return connections_;
  }
  LowEnergyConnector* connector() const { return connector_.get(); }

 private:
  void OnConnectionCreated(std::unique_ptr<Connection> connection) {
    connections_.push_back(std::move(connection));

    if (quit_loop_on_new_connection)
      message_loop()->QuitNow();
  }

  void OnConnectionStateChanged(const common::DeviceAddress& address,
                                bool connected,
                                bool canceled) {
    request_canceled = canceled;
    if (request_canceled && quit_loop_on_cancel)
      message_loop()->QuitNow();
  }

  std::unique_ptr<LowEnergyConnector> connector_;
  std::vector<std::unique_ptr<Connection>> connections_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyConnectorTest);
};

TEST_F(LowEnergyConnectorTest, CreateConnection) {
  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);
  test_device()->AddLEDevice(std::move(fake_device));

  EXPECT_FALSE(connector()->request_pending());

  hci::LowEnergyConnector::Result result;
  hci::Status hci_status;
  bool callback_called = false;

  auto callback = [&, this](auto cb_result, auto cb_status) {
    result = cb_result;
    hci_status = cb_status;
    callback_called = true;

    message_loop()->PostQuitTask();
  };

  hci::Connection::LowEnergyParameters params;
  bool ret = connector()->CreateConnection(
      hci::LEOwnAddressType::kPublic, false, kTestAddress,
      defaults::kLEScanInterval, defaults::kLEScanWindow, params, callback,
      kTestTimeoutMs);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(connector()->request_pending());

  ret = connector()->CreateConnection(hci::LEOwnAddressType::kPublic, false,
                                      kTestAddress, defaults::kLEScanInterval,
                                      defaults::kLEScanWindow, params, callback,
                                      kTestTimeoutMs);
  EXPECT_FALSE(ret);

  RunMessageLoop();

  EXPECT_FALSE(connector()->request_pending());
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(hci::LowEnergyConnector::Result::kSuccess, result);
  EXPECT_EQ(hci::Status::kSuccess, hci_status);
  ASSERT_EQ(1u, connections().size());

  auto conn = connections()[0].get();
  EXPECT_EQ(1u, conn->handle());
  EXPECT_EQ(kTestAddress, conn->peer_address());
  EXPECT_TRUE(conn->is_open());
  conn->set_closed();
}

// Controller reports error from HCI Command Status event.
TEST_F(LowEnergyConnectorTest, CreateConnectionStatusError) {
  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);
  fake_device->set_connect_status(hci::Status::kCommandDisallowed);
  test_device()->AddLEDevice(std::move(fake_device));

  EXPECT_FALSE(connector()->request_pending());

  hci::LowEnergyConnector::Result result;
  hci::Status hci_status;
  bool callback_called = false;

  auto callback = [&, this](auto cb_result, auto cb_status) {
    result = cb_result;
    hci_status = cb_status;
    callback_called = true;

    message_loop()->PostQuitTask();
  };

  hci::Connection::LowEnergyParameters params;
  bool ret = connector()->CreateConnection(
      hci::LEOwnAddressType::kPublic, false, kTestAddress,
      defaults::kLEScanInterval, defaults::kLEScanWindow, params, callback,
      kTestTimeoutMs);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(connector()->request_pending());

  RunMessageLoop();

  EXPECT_FALSE(connector()->request_pending());
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(hci::LowEnergyConnector::Result::kFailed, result);
  EXPECT_EQ(hci::Status::kCommandDisallowed, hci_status);
  EXPECT_TRUE(connections().empty());
}

// Controller reports error from HCI LE Connection Complete event
TEST_F(LowEnergyConnectorTest, CreateConnectionEventError) {
  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);
  fake_device->set_connect_response(hci::Status::kConnectionRejectedSecurity);
  test_device()->AddLEDevice(std::move(fake_device));

  EXPECT_FALSE(connector()->request_pending());

  hci::LowEnergyConnector::Result result;
  hci::Status hci_status;
  bool callback_called = false;

  auto callback = [&, this](auto cb_result, auto cb_status) {
    result = cb_result;
    hci_status = cb_status;
    callback_called = true;

    message_loop()->PostQuitTask();
  };

  hci::Connection::LowEnergyParameters params;
  bool ret = connector()->CreateConnection(
      hci::LEOwnAddressType::kPublic, false, kTestAddress,
      defaults::kLEScanInterval, defaults::kLEScanWindow, params, callback,
      kTestTimeoutMs);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(connector()->request_pending());

  RunMessageLoop();

  EXPECT_FALSE(connector()->request_pending());
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(hci::LowEnergyConnector::Result::kFailed, result);
  EXPECT_EQ(hci::Status::kConnectionRejectedSecurity, hci_status);
  EXPECT_TRUE(connections().empty());
}

// Controller reports error from HCI LE Connection Complete event
TEST_F(LowEnergyConnectorTest, Cancel) {
  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);
  test_device()->AddLEDevice(std::move(fake_device));

  hci::LowEnergyConnector::Result result;
  hci::Status hci_status;
  bool callback_called = false;

  auto callback = [&, this](auto cb_result, auto cb_status) {
    result = cb_result;
    hci_status = cb_status;
    callback_called = true;

    message_loop()->PostQuitTask();
  };

  hci::Connection::LowEnergyParameters params;
  bool ret = connector()->CreateConnection(
      hci::LEOwnAddressType::kPublic, false, kTestAddress,
      defaults::kLEScanInterval, defaults::kLEScanWindow, params, callback,
      kTestTimeoutMs);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(connector()->request_pending());

  ASSERT_FALSE(request_canceled);
  connector()->Cancel();
  EXPECT_TRUE(connector()->request_pending());

  RunMessageLoop();

  EXPECT_FALSE(connector()->request_pending());
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(request_canceled);
  EXPECT_EQ(hci::LowEnergyConnector::Result::kCanceled, result);
  EXPECT_EQ(hci::Status::kUnknownConnectionId, hci_status);
  EXPECT_TRUE(connections().empty());
}

TEST_F(LowEnergyConnectorTest, IncomingConnect) {
  EXPECT_TRUE(connections().empty());
  EXPECT_FALSE(connector()->request_pending());

  LEConnectionCompleteSubeventParams event;
  std::memset(&event, 0, sizeof(event));

  event.status = Status::kSuccess;
  event.peer_address = kTestAddress.value();
  event.peer_address_type = LEPeerAddressType::kPublic;
  event.conn_interval = defaults::kLEConnectionIntervalMin;
  event.connection_handle = 1;

  test_device()->SendLEMetaEvent(hci::kLEConnectionCompleteSubeventCode, &event,
                                 sizeof(event));

  quit_loop_on_new_connection = true;
  RunMessageLoop();

  ASSERT_EQ(1u, connections().size());

  auto conn = connections()[0].get();
  EXPECT_EQ(1u, conn->handle());
  EXPECT_EQ(kTestAddress, conn->peer_address());
  EXPECT_TRUE(conn->is_open());
  conn->set_closed();
}

TEST_F(LowEnergyConnectorTest, IncomingConnectDuringConnectionRequest) {
  const common::DeviceAddress kIncomingAddress(
      common::DeviceAddress::Type::kLEPublic, "00:00:00:00:00:02");

  EXPECT_TRUE(connections().empty());
  EXPECT_FALSE(connector()->request_pending());

  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);
  test_device()->AddLEDevice(std::move(fake_device));

  hci::LowEnergyConnector::Result result;
  hci::Status hci_status;
  unsigned int callback_count = 0;

  auto callback = [&, this](auto cb_result, auto cb_status) {
    result = cb_result;
    hci_status = cb_status;
    callback_count++;

    message_loop()->PostQuitTask();
  };

  hci::Connection::LowEnergyParameters params;
  connector()->CreateConnection(hci::LEOwnAddressType::kPublic, false,
                                kTestAddress, defaults::kLEScanInterval,
                                defaults::kLEScanWindow, params, callback,
                                kTestTimeoutMs);

  message_loop()->task_runner()->PostTask([kIncomingAddress, this] {
    LEConnectionCompleteSubeventParams event;
    std::memset(&event, 0, sizeof(event));

    event.status = Status::kSuccess;
    event.peer_address = kIncomingAddress.value();
    event.peer_address_type = LEPeerAddressType::kPublic;
    event.conn_interval = defaults::kLEConnectionIntervalMin;
    event.connection_handle = 2;

    test_device()->SendLEMetaEvent(hci::kLEConnectionCompleteSubeventCode,
                                   &event, sizeof(event));
  });

  RunMessageLoop();

  EXPECT_EQ(hci::LowEnergyConnector::Result::kSuccess, result);
  EXPECT_EQ(hci::Status::kSuccess, hci_status);
  EXPECT_EQ(1u, callback_count);
  ASSERT_EQ(2u, connections().size());

  for (const auto& conn : connections()) {
    EXPECT_TRUE(conn->handle() == 1u || conn->handle() == 2u);
    EXPECT_TRUE(conn->peer_address() == kTestAddress ||
                conn->peer_address() == kIncomingAddress);
    EXPECT_TRUE(conn->is_open());
    conn->set_closed();
  }
}

TEST_F(LowEnergyConnectorTest, CreateConnectionTimeout) {
  constexpr int64_t kShortTimeoutMs = 10;

  // We do not set up any fake devices. This will cause the request to time out.
  EXPECT_FALSE(connector()->request_pending());

  hci::LowEnergyConnector::Result result;
  hci::Status hci_status;
  bool callback_called = false;

  auto callback = [&, this](auto cb_result, auto cb_status) {
    result = cb_result;
    hci_status = cb_status;
    callback_called = true;

    message_loop()->PostQuitTask();
  };

  hci::Connection::LowEnergyParameters params;
  connector()->CreateConnection(hci::LEOwnAddressType::kPublic, false,
                                kTestAddress, defaults::kLEScanInterval,
                                defaults::kLEScanWindow, params, callback,
                                kShortTimeoutMs);
  EXPECT_TRUE(connector()->request_pending());

  EXPECT_FALSE(request_canceled);

  RunMessageLoop();

  EXPECT_FALSE(connector()->request_pending());
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(hci::LowEnergyConnector::Result::kCanceled, result);
  EXPECT_TRUE(request_canceled);
  EXPECT_EQ(hci::Status::kCommandTimeout, hci_status);
  EXPECT_TRUE(connections().empty());
}

TEST_F(LowEnergyConnectorTest, SendRequestAndDelete) {
  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);
  test_device()->AddLEDevice(std::move(fake_device));

  hci::Connection::LowEnergyParameters params;
  bool ret = connector()->CreateConnection(
      hci::LEOwnAddressType::kPublic, false, kTestAddress,
      defaults::kLEScanInterval, defaults::kLEScanWindow, params,
      [](auto, auto) {}, kTestTimeoutMs);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(connector()->request_pending());

  DeleteConnector();

  quit_loop_on_cancel = true;
  RunMessageLoop();

  EXPECT_TRUE(request_canceled);
  EXPECT_TRUE(connections().empty());
}

// This test is identical to SendRequestAndDelete except that this waits for the
// connection request timeout to finish.
TEST_F(LowEnergyConnectorTest, SendRequestDeleteAndWaitForTimeout) {
  constexpr int64_t kShortTimeoutMs = 100;
  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);
  test_device()->AddLEDevice(std::move(fake_device));

  hci::Connection::LowEnergyParameters params;
  bool ret = connector()->CreateConnection(
      hci::LEOwnAddressType::kPublic, false, kTestAddress,
      defaults::kLEScanInterval, defaults::kLEScanWindow, params,
      [](auto, auto) {}, kShortTimeoutMs);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(connector()->request_pending());

  DeleteConnector();

  // Run the message loop long enough for the connection creation timeout to
  // expire. The timeout handler should be canceled during destruction and no
  // assertions should be hit.
  RunMessageLoop(fxl::TimeDelta::FromMilliseconds(2 * kShortTimeoutMs));

  EXPECT_TRUE(request_canceled);
  EXPECT_TRUE(connections().empty());
}

}  // namespace
}  // namespace hci
}  // namespace bluetooth
