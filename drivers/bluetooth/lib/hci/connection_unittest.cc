// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/hci/connection.h"

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/testing/test_base.h"
#include "apps/bluetooth/lib/testing/test_controller.h"

namespace bluetooth {
namespace hci {
namespace {

constexpr ConnectionHandle kTestHandle = 0x0001;
constexpr Connection::Role kTestRole = Connection::Role::kMaster;
const Connection::LowEnergyParameters kTestParams(1, 2, 1, 1, 1);
const common::DeviceAddress kTestAddress(common::DeviceAddress::Type::kLEPublic,
                                         "00:00:00:00:00:01");

using ::bluetooth::testing::CommandTransaction;

using TestingBase = ::bluetooth::testing::TransportTest<::bluetooth::testing::TestController>;

class ConnectionTest : public TestingBase {
 public:
  ConnectionTest() = default;
  ~ConnectionTest() override = default;
};

TEST_F(ConnectionTest, TestGetters) {
  Connection connection(kTestHandle, kTestRole, kTestAddress, kTestParams, transport());

  EXPECT_EQ(Connection::LinkType::kLE, connection.ll_type());
  EXPECT_EQ(kTestHandle, connection.handle());
  EXPECT_EQ(kTestRole, connection.role());
  EXPECT_EQ(kTestParams, connection.low_energy_parameters());
  EXPECT_EQ(kTestAddress, connection.peer_address());
  EXPECT_TRUE(connection.is_open());
}

TEST_F(ConnectionTest, Close) {
  // clang-format off

  // HCI_Disconnect (handle: 0x0001, reason: RemoteUserTerminatedConnection)
  auto req_bytes = common::CreateStaticByteBuffer(
      0x06, 0x04, 0x03, 0x01, 0x00, Status::kRemoteUserTerminatedConnection);

  // Respond with Command Status and Disconnection Complete.
  auto cmd_status_bytes = common::CreateStaticByteBuffer(
      kCommandStatusEventCode, 0x04, Status::kSuccess, 1, 0x06, 0x04);

  auto disc_cmpl_bytes = common::CreateStaticByteBuffer(
      kDisconnectionCompleteEventCode, 0x04,
      Status::kSuccess, 0x01, 0x00, Status::kConnectionTerminatedByLocalHost);

  // clang-format on

  test_device()->QueueCommandTransaction(
      CommandTransaction({req_bytes, {&cmd_status_bytes, &disc_cmpl_bytes}}));
  test_device()->Start();

  bool callback_called = false;
  test_device()->SetTransactionCallback(
      [&callback_called, this] {
        callback_called = true;
        message_loop()->QuitNow();
      },
      message_loop()->task_runner());

  Connection connection(kTestHandle, kTestRole, kTestAddress, kTestParams, transport());
  EXPECT_TRUE(connection.is_open());

  connection.Close(Status::kRemoteUserTerminatedConnection);
  EXPECT_FALSE(connection.is_open());

  RunMessageLoop();
  EXPECT_TRUE(callback_called);
}

TEST_F(ConnectionTest, CloseError) {
  // clang-format off

  // HCI_Disconnect (handle: 0x0001, reason: RemoteUserTerminatedConnection)
  auto req_bytes = common::CreateStaticByteBuffer(
      0x06, 0x04, 0x03, 0x01, 0x00, Status::kRemoteUserTerminatedConnection);

  // Respond with Command Status and Disconnection Complete.
  auto cmd_status_bytes = common::CreateStaticByteBuffer(
      kCommandStatusEventCode, 0x04, Status::kSuccess, 1, 0x06, 0x04);

  auto disc_cmpl_bytes = common::CreateStaticByteBuffer(
      kDisconnectionCompleteEventCode, 0x04,
      Status::kCommandDisallowed, 0x01, 0x00, Status::kConnectionTerminatedByLocalHost);

  // clang-format on

  test_device()->QueueCommandTransaction(
      CommandTransaction({req_bytes, {&cmd_status_bytes, &disc_cmpl_bytes}}));
  test_device()->Start();

  // The callback should get called regardless of the procedure status.
  bool callback_called = false;
  test_device()->SetTransactionCallback(
      [&callback_called, this] {
        callback_called = true;
        message_loop()->QuitNow();
      },
      message_loop()->task_runner());

  Connection connection(kTestHandle, kTestRole, kTestAddress, kTestParams, transport());
  EXPECT_TRUE(connection.is_open());

  connection.Close(Status::kRemoteUserTerminatedConnection);
  EXPECT_FALSE(connection.is_open());

  RunMessageLoop();
  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace hci
}  // namespace bluetooth
