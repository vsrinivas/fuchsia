// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"

#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/test_controller.h"

namespace btlib {
namespace hci {
namespace {

constexpr OpCode kTestOpCode = 0xFFFF;

using ::btlib::testing::CommandTransaction;

using TestingBase =
    ::btlib::testing::FakeControllerTest<::btlib::testing::TestController>;

class SequentialCommandRunnerTest : public TestingBase {
 public:
  SequentialCommandRunnerTest() = default;
  ~SequentialCommandRunnerTest() override = default;
};

using HCI_SequentialCommandRunnerTest = SequentialCommandRunnerTest;

TEST_F(HCI_SequentialCommandRunnerTest, SequentialCommandRunner) {
  // HCI command with custom opcode FFFF.
  auto command_bytes = common::CreateStaticByteBuffer(0xFF, 0xFF, 0x00);

  auto command_status_error_bytes = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      StatusCode::kHardwareFailure, 1, 0xFF, 0xFF);

  auto command_cmpl_error_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, StatusCode::kReserved0);

  auto command_cmpl_success_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, StatusCode::kSuccess);

  // Here we perform multiple test sequences where we queue up several  commands
  // in each sequence. We expect each sequence to terminate differently after
  // the following HCI transactions:
  //
  // Sequence 1 (HCI packets)
  //    -> Command; <- error status
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_status_error_bytes}));

  // Sequence 2 (HCI packets)
  //    -> Command; <- error complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_error_bytes}));

  // Sequence 3 (HCI packets)
  //    -> Command; <- success complete
  //    -> Command; <- error complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_error_bytes}));

  // Sequence 4 (HCI packets)
  //    -> Command; <- success complete
  //    -> Command; <- success complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));

  // Sequence 5 (HCI packets)
  //    -> Command; <- success complete
  //    -> Command; <- success complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));

  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  Status status;
  int status_cb_called = 0;
  auto status_cb = [&, this](Status cb_status) {
    status = cb_status;
    status_cb_called++;
  };

  int cb_called = 0;
  auto cb = [&](const EventPacket& event) { cb_called++; };

  // Sequence 1 (test)
  SequentialCommandRunner cmd_runner(dispatcher(), transport());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(1, status_cb_called);
  EXPECT_EQ(StatusCode::kHardwareFailure, status.protocol_error());

  // Sequence 2 (test)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(2, status_cb_called);
  EXPECT_EQ(StatusCode::kReserved0, status.protocol_error());

  // Sequence 3 (test)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(1, cb_called);
  EXPECT_EQ(3, status_cb_called);
  EXPECT_EQ(StatusCode::kReserved0, status.protocol_error());
  cb_called = 0;

  // Sequence 4 (test)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(2, cb_called);
  EXPECT_EQ(4, status_cb_called);
  EXPECT_TRUE(status);
  cb_called = 0;
  status_cb_called = 0;

  // Sequence 5 (test) (no callback passed to QueueCommand)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode));
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode));

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(1, status_cb_called);
  EXPECT_TRUE(status);
}

TEST_F(HCI_SequentialCommandRunnerTest, SequentialCommandRunnerCancel) {
  auto command_bytes = common::CreateStaticByteBuffer(0xFF, 0xFF, 0x00);

  auto command_cmpl_error_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, StatusCode::kHardwareFailure);

  auto command_cmpl_success_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, StatusCode::kSuccess);

  // Sequence 1
  //   -> Command; <- success complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));

  // Sequence 2
  //   -> Command; <- success complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));

  // Sequence 3
  //   -> Command; <- success complete
  //   -> Command; <- error complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_error_bytes}));

  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  Status status;
  int status_cb_called = 0;
  auto status_cb = [&, this](Status cb_status) {
    status = cb_status;
    status_cb_called++;
  };

  int cb_called = 0;
  auto cb = [&](const EventPacket& event) { cb_called++; };

  // Sequence 1: Sequence will be cancelled after the first command.
  SequentialCommandRunner cmd_runner(dispatcher(), transport());
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  // Call RunCommands() and right away post a task to cancel the sequence. The
  // first command will go out but no successive packets should be sent and no
  // callbacks should be invoked.
  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  cmd_runner.Cancel();

  // Since |status_cb| is expected to not get called (which would normally quit
  // the message loop) - we run until we reach a steady-state waiting.
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(0, status_cb_called);

  // Sequence 2: Sequence will be cancelled after first command. This tests
  // canceling a sequence from a CommandCompleteCallback.
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          [&](const EventPacket& event) {
                            cmd_runner.Cancel();
                            EXPECT_TRUE(cmd_runner.IsReady());
                            EXPECT_FALSE(cmd_runner.HasQueuedCommands());
                          });
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());

  // |status_cb| is expected to not get called.
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(0, status_cb_called);

  // Sequence 3: Sequence will be cancelled after first command and immediately
  // followed by a second command which will fail. This tests canceling a
  // sequence and initiating a new one from a CommandCompleteCallback.
  cmd_runner.QueueCommand(
      CommandPacket::New(kTestOpCode), [&](const EventPacket& event) {
        cmd_runner.Cancel();
        EXPECT_TRUE(cmd_runner.IsReady());
        EXPECT_FALSE(cmd_runner.HasQueuedCommands());

        // Queue multiple commands (only one will execute since TestController
        // will send back an error status.
        cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
        cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                                cb);  // <-- Should not run
        cmd_runner.RunCommands(status_cb);
      });
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());

  RunLoopUntilIdle();

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  // The result callback should have been called once with a failure result.
  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(1, status_cb_called);
  EXPECT_EQ(StatusCode::kHardwareFailure, status.protocol_error());
}

}  // namespace
}  // namespace hci
}  // namespace btlib
