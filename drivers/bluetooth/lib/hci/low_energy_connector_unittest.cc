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

namespace btlib {
namespace hci {
namespace {

using ::btlib::testing::FakeController;
using ::btlib::testing::FakeDevice;
using TestingBase = ::btlib::testing::FakeControllerTest<FakeController>;

const common::DeviceAddress kTestAddress(common::DeviceAddress::Type::kLEPublic,
                                         "00:00:00:00:00:01");
const LEPreferredConnectionParameters kTestParams(1, 1, 1, 1);
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
        std::bind(&LowEnergyConnectorTest::OnIncomingConnectionCreated, this,
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

  const std::vector<std::unique_ptr<Connection>>& in_connections() const {
    return in_connections_;
  }
  LowEnergyConnector* connector() const { return connector_.get(); }

 private:
  void OnIncomingConnectionCreated(std::unique_ptr<Connection> connection) {
    in_connections_.push_back(std::move(connection));

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

  // Incoming connections.
  std::vector<std::unique_ptr<Connection>> in_connections_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyConnectorTest);
};

using HCI_LowEnergyConnectorTest = LowEnergyConnectorTest;

TEST_F(HCI_LowEnergyConnectorTest, CreateConnection) {
  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);
  test_device()->AddLEDevice(std::move(fake_device));

  EXPECT_FALSE(connector()->request_pending());

  LowEnergyConnector::Result result;
  Status hci_status;
  ConnectionPtr conn;
  bool callback_called = false;

  auto callback = [&, this](auto cb_result, auto cb_status, auto cb_conn) {
    result = cb_result;
    hci_status = cb_status;
    conn = std::move(cb_conn);
    callback_called = true;

    message_loop()->PostQuitTask();
  };

  bool ret = connector()->CreateConnection(
      LEOwnAddressType::kPublic, false, kTestAddress, defaults::kLEScanInterval,
      defaults::kLEScanWindow, kTestParams, callback, kTestTimeoutMs);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(connector()->request_pending());

  ret = connector()->CreateConnection(
      LEOwnAddressType::kPublic, false, kTestAddress, defaults::kLEScanInterval,
      defaults::kLEScanWindow, kTestParams, callback, kTestTimeoutMs);
  EXPECT_FALSE(ret);

  RunMessageLoop();

  EXPECT_FALSE(connector()->request_pending());
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(LowEnergyConnector::Result::kSuccess, result);
  EXPECT_EQ(Status::kSuccess, hci_status);
  EXPECT_TRUE(in_connections().empty());

  ASSERT_TRUE(conn);
  EXPECT_EQ(1u, conn->handle());
  EXPECT_EQ(kTestAddress, conn->peer_address());
  EXPECT_TRUE(conn->is_open());
  conn->set_closed();
}

// Controller reports error from HCI Command Status event.
TEST_F(HCI_LowEnergyConnectorTest, CreateConnectionStatusError) {
  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);
  fake_device->set_connect_status(Status::kCommandDisallowed);
  test_device()->AddLEDevice(std::move(fake_device));

  EXPECT_FALSE(connector()->request_pending());

  LowEnergyConnector::Result result;
  Status hci_status;
  ConnectionPtr conn;
  bool callback_called = false;

  auto callback = [&, this](auto cb_result, auto cb_status, auto cb_conn) {
    result = cb_result;
    hci_status = cb_status;
    conn = std::move(cb_conn);
    callback_called = true;

    message_loop()->PostQuitTask();
  };

  bool ret = connector()->CreateConnection(
      LEOwnAddressType::kPublic, false, kTestAddress, defaults::kLEScanInterval,
      defaults::kLEScanWindow, kTestParams, callback, kTestTimeoutMs);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(connector()->request_pending());

  RunMessageLoop();

  EXPECT_FALSE(connector()->request_pending());
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(LowEnergyConnector::Result::kFailed, result);
  EXPECT_EQ(Status::kCommandDisallowed, hci_status);
  EXPECT_FALSE(conn);
  EXPECT_TRUE(in_connections().empty());
}

// Controller reports error from HCI LE Connection Complete event
TEST_F(HCI_LowEnergyConnectorTest, CreateConnectionEventError) {
  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);
  fake_device->set_connect_response(Status::kConnectionRejectedSecurity);
  test_device()->AddLEDevice(std::move(fake_device));

  EXPECT_FALSE(connector()->request_pending());

  LowEnergyConnector::Result result;
  Status hci_status;
  ConnectionPtr conn;
  bool callback_called = false;

  auto callback = [&, this](auto cb_result, auto cb_status, auto cb_conn) {
    result = cb_result;
    hci_status = cb_status;
    callback_called = true;
    conn = std::move(cb_conn);

    message_loop()->PostQuitTask();
  };

  bool ret = connector()->CreateConnection(
      LEOwnAddressType::kPublic, false, kTestAddress, defaults::kLEScanInterval,
      defaults::kLEScanWindow, kTestParams, callback, kTestTimeoutMs);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(connector()->request_pending());

  RunMessageLoop();

  EXPECT_FALSE(connector()->request_pending());
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(LowEnergyConnector::Result::kFailed, result);
  EXPECT_EQ(Status::kConnectionRejectedSecurity, hci_status);
  EXPECT_TRUE(in_connections().empty());
  EXPECT_FALSE(conn);
}

// Controller reports error from HCI LE Connection Complete event
TEST_F(HCI_LowEnergyConnectorTest, Cancel) {
  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);

  // Make sure the pending connect remains pending.
  fake_device->set_force_pending_connect(true);
  test_device()->AddLEDevice(std::move(fake_device));

  LowEnergyConnector::Result result;
  Status hci_status;
  ConnectionPtr conn;
  bool callback_called = false;

  auto callback = [&, this](auto cb_result, auto cb_status, auto cb_conn) {
    result = cb_result;
    hci_status = cb_status;
    callback_called = true;
    conn = std::move(cb_conn);

    message_loop()->PostQuitTask();
  };

  bool ret = connector()->CreateConnection(
      LEOwnAddressType::kPublic, false, kTestAddress, defaults::kLEScanInterval,
      defaults::kLEScanWindow, kTestParams, callback, kTestTimeoutMs);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(connector()->request_pending());

  ASSERT_FALSE(request_canceled);

  connector()->Cancel();
  EXPECT_TRUE(connector()->request_pending());

  // The request timeout should be canceled regardless of whether it was posted
  // before.
  EXPECT_FALSE(connector()->timeout_posted());

  RunMessageLoop();

  EXPECT_FALSE(connector()->timeout_posted());
  EXPECT_FALSE(connector()->request_pending());
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(request_canceled);
  EXPECT_EQ(LowEnergyConnector::Result::kCanceled, result);
  EXPECT_EQ(Status::kUnknownConnectionId, hci_status);
  EXPECT_TRUE(in_connections().empty());
  EXPECT_FALSE(conn);
}

TEST_F(HCI_LowEnergyConnectorTest, IncomingConnect) {
  EXPECT_TRUE(in_connections().empty());
  EXPECT_FALSE(connector()->request_pending());

  LEConnectionCompleteSubeventParams event;
  std::memset(&event, 0, sizeof(event));

  event.status = Status::kSuccess;
  event.peer_address = kTestAddress.value();
  event.peer_address_type = LEPeerAddressType::kPublic;
  event.conn_interval = defaults::kLEConnectionIntervalMin;
  event.connection_handle = 1;

  test_device()->SendLEMetaEvent(kLEConnectionCompleteSubeventCode,
                                 common::BufferView(&event, sizeof(event)));

  quit_loop_on_new_connection = true;
  RunMessageLoop();

  ASSERT_EQ(1u, in_connections().size());

  auto conn = in_connections()[0].get();
  EXPECT_EQ(1u, conn->handle());
  EXPECT_EQ(kTestAddress, conn->peer_address());
  EXPECT_TRUE(conn->is_open());
  conn->set_closed();
}

TEST_F(HCI_LowEnergyConnectorTest, IncomingConnectDuringConnectionRequest) {
  const common::DeviceAddress kIncomingAddress(
      common::DeviceAddress::Type::kLEPublic, "00:00:00:00:00:02");

  EXPECT_TRUE(in_connections().empty());
  EXPECT_FALSE(connector()->request_pending());

  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);
  test_device()->AddLEDevice(std::move(fake_device));

  LowEnergyConnector::Result result;
  Status hci_status;
  ConnectionPtr conn;
  unsigned int callback_count = 0;

  auto callback = [&, this](auto cb_result, auto cb_status, auto cb_conn) {
    result = cb_result;
    hci_status = cb_status;
    callback_count++;
    conn = std::move(cb_conn);

    message_loop()->PostQuitTask();
  };

  connector()->CreateConnection(
      LEOwnAddressType::kPublic, false, kTestAddress, defaults::kLEScanInterval,
      defaults::kLEScanWindow, kTestParams, callback, kTestTimeoutMs);

  message_loop()->task_runner()->PostTask([kIncomingAddress, this] {
    LEConnectionCompleteSubeventParams event;
    std::memset(&event, 0, sizeof(event));

    event.status = Status::kSuccess;
    event.peer_address = kIncomingAddress.value();
    event.peer_address_type = LEPeerAddressType::kPublic;
    event.conn_interval = defaults::kLEConnectionIntervalMin;
    event.connection_handle = 2;

    test_device()->SendLEMetaEvent(kLEConnectionCompleteSubeventCode,
                                   common::BufferView(&event, sizeof(event)));
  });

  RunMessageLoop();

  EXPECT_EQ(LowEnergyConnector::Result::kSuccess, result);
  EXPECT_EQ(Status::kSuccess, hci_status);
  EXPECT_EQ(1u, callback_count);
  ASSERT_EQ(1u, in_connections().size());

  const auto& in_conn = in_connections().front();

  EXPECT_EQ(1u, conn->handle());
  EXPECT_EQ(2u, in_conn->handle());
  EXPECT_EQ(kTestAddress, conn->peer_address());
  EXPECT_EQ(kIncomingAddress, in_conn->peer_address());

  EXPECT_TRUE(conn->is_open());
  EXPECT_TRUE(in_conn->is_open());
  conn->set_closed();
  in_conn->set_closed();
}

TEST_F(HCI_LowEnergyConnectorTest, CreateConnectionTimeout) {
  constexpr int64_t kShortTimeoutMs = 10;

  // We do not set up any fake devices. This will cause the request to time out.
  EXPECT_FALSE(connector()->request_pending());

  LowEnergyConnector::Result result;
  Status hci_status;
  ConnectionPtr conn;
  bool callback_called = false;

  auto callback = [&, this](auto cb_result, auto cb_status, auto cb_conn) {
    result = cb_result;
    hci_status = cb_status;
    callback_called = true;
    conn = std::move(cb_conn);

    message_loop()->PostQuitTask();
  };

  connector()->CreateConnection(
      LEOwnAddressType::kPublic, false, kTestAddress, defaults::kLEScanInterval,
      defaults::kLEScanWindow, kTestParams, callback, kShortTimeoutMs);
  EXPECT_TRUE(connector()->request_pending());

  EXPECT_FALSE(request_canceled);

  RunMessageLoop();

  EXPECT_FALSE(connector()->request_pending());
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(LowEnergyConnector::Result::kCanceled, result);
  EXPECT_TRUE(request_canceled);
  EXPECT_EQ(Status::kCommandTimeout, hci_status);
  EXPECT_TRUE(in_connections().empty());
  EXPECT_FALSE(conn);
}

TEST_F(HCI_LowEnergyConnectorTest, SendRequestAndDelete) {
  auto fake_device = std::make_unique<FakeDevice>(kTestAddress, true, true);

  // Make sure the pending connect remains pending.
  fake_device->set_force_pending_connect(true);
  test_device()->AddLEDevice(std::move(fake_device));

  bool ret = connector()->CreateConnection(
      LEOwnAddressType::kPublic, false, kTestAddress, defaults::kLEScanInterval,
      defaults::kLEScanWindow, kTestParams, [](auto, auto, auto) {},
      kTestTimeoutMs);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(connector()->request_pending());

  DeleteConnector();

  quit_loop_on_cancel = true;
  RunMessageLoop();

  EXPECT_TRUE(request_canceled);
  EXPECT_TRUE(in_connections().empty());
}

}  // namespace
}  // namespace hci
}  // namespace btlib
