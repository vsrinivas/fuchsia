// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/hci/connection.h"

#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/test_controller.h"

namespace btlib {
namespace hci {
namespace {

constexpr ConnectionHandle kTestHandle = 0x0001;
constexpr Connection::Role kTestRole = Connection::Role::kMaster;
const LEConnectionParameters kTestParams(1, 1, 1);
const common::DeviceAddress kTestAddress(common::DeviceAddress::Type::kLEPublic,
                                         "00:00:00:00:00:01");

using ::btlib::testing::CommandTransaction;

using TestingBase =
    ::btlib::testing::FakeControllerTest<::btlib::testing::TestController>;

class ConnectionTest : public TestingBase {
 public:
  ConnectionTest() = default;
  ~ConnectionTest() override = default;
};

using HCI_ConnectionTest = ConnectionTest;

TEST_F(HCI_ConnectionTest, TestGetters) {
  Connection connection(kTestHandle, kTestRole, kTestAddress, kTestParams,
                        transport());

  EXPECT_EQ(Connection::LinkType::kLE, connection.ll_type());
  EXPECT_EQ(kTestHandle, connection.handle());
  EXPECT_EQ(kTestRole, connection.role());
  EXPECT_EQ(kTestParams, connection.low_energy_parameters());
  EXPECT_EQ(kTestAddress, connection.peer_address());
  EXPECT_TRUE(connection.is_open());
}

TEST_F(HCI_ConnectionTest, Close) {
  // clang-format off

  // HCI_Disconnect (handle: 0x0001, reason: RemoteUserTerminatedConnection)
  auto req_bytes = common::CreateStaticByteBuffer(
      0x06, 0x04, 0x03, 0x01, 0x00, StatusCode::kRemoteUserTerminatedConnection);

  // Respond with Command Status and Disconnection Complete.
  auto cmd_status_bytes = common::CreateStaticByteBuffer(
      kCommandStatusEventCode, 0x04, StatusCode::kSuccess, 1, 0x06, 0x04);

  auto disc_cmpl_bytes = common::CreateStaticByteBuffer(
      kDisconnectionCompleteEventCode, 0x04,
      StatusCode::kSuccess, 0x01, 0x00, StatusCode::kConnectionTerminatedByLocalHost);

  // clang-format on

  test_device()->QueueCommandTransaction(
      CommandTransaction({req_bytes, {&cmd_status_bytes, &disc_cmpl_bytes}}));
  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  bool callback_called = false;
  test_device()->SetTransactionCallback(
      [&callback_called, this] {
        callback_called = true;
      },
      dispatcher());

  Connection connection(kTestHandle, kTestRole, kTestAddress, kTestParams,
                        transport());
  EXPECT_TRUE(connection.is_open());

  connection.Close(StatusCode::kRemoteUserTerminatedConnection);
  EXPECT_FALSE(connection.is_open());

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(HCI_ConnectionTest, CloseError) {
  // clang-format off

  // HCI_Disconnect (handle: 0x0001, reason: RemoteUserTerminatedConnection)
  auto req_bytes = common::CreateStaticByteBuffer(
      0x06, 0x04, 0x03, 0x01, 0x00, StatusCode::kRemoteUserTerminatedConnection);

  // Respond with Command Status and Disconnection Complete.
  auto cmd_status_bytes = common::CreateStaticByteBuffer(
      kCommandStatusEventCode, 0x04, StatusCode::kSuccess, 1, 0x06, 0x04);

  auto disc_cmpl_bytes = common::CreateStaticByteBuffer(
      kDisconnectionCompleteEventCode, 0x04,
      StatusCode::kCommandDisallowed, 0x01, 0x00, StatusCode::kConnectionTerminatedByLocalHost);

  // clang-format on

  test_device()->QueueCommandTransaction(
      CommandTransaction({req_bytes, {&cmd_status_bytes, &disc_cmpl_bytes}}));
  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  // The callback should get called regardless of the procedure status.
  bool callback_called = false;
  test_device()->SetTransactionCallback(
      [&callback_called, this] {
        callback_called = true;
      },
      dispatcher());

  Connection connection(kTestHandle, kTestRole, kTestAddress, kTestParams,
                        transport());
  EXPECT_TRUE(connection.is_open());

  connection.Close(StatusCode::kRemoteUserTerminatedConnection);
  EXPECT_FALSE(connection.is_open());

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace hci
}  // namespace btlib
