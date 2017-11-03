// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"

#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/test_controller.h"

namespace bluetooth {
namespace hci {
namespace {

constexpr OpCode kTestOpCode = 0xFFFF;

using ::bluetooth::testing::CommandTransaction;

using TestingBase = ::bluetooth::testing::FakeControllerTest<
    ::bluetooth::testing::TestController>;

class SequentialCommandRunnerTest : public TestingBase {
 public:
  SequentialCommandRunnerTest() = default;
  ~SequentialCommandRunnerTest() override = default;
};

TEST_F(SequentialCommandRunnerTest, SequentialCommandRunner) {
  // HCI command with custom opcode FFFF.
  auto command_bytes = common::CreateStaticByteBuffer(0xFF, 0xFF, 0x00);

  auto command_status_error_bytes = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kHardwareFailure, 1, 0xFF, 0xFF);

  auto command_cmpl_error_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, Status::kHardwareFailure);

  auto command_cmpl_success_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, Status::kSuccess);

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

  test_device()->Start();

  bool result;
  int result_cb_called = 0;
  auto result_cb = [&, this](bool cb_result) {
    result = cb_result;
    result_cb_called++;
    message_loop()->QuitNow();
  };

  int cb_called = 0;
  auto cb = [&](const EventPacket& event) { cb_called++; };

  // Sequence 1 (test)
  SequentialCommandRunner cmd_runner(message_loop()->task_runner(),
                                     transport());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunMessageLoop();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(1, result_cb_called);
  EXPECT_FALSE(result);

  // Sequence 2 (test)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunMessageLoop();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(2, result_cb_called);
  EXPECT_FALSE(result);

  // Sequence 3 (test)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunMessageLoop();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(1, cb_called);
  EXPECT_EQ(3, result_cb_called);
  EXPECT_FALSE(result);
  cb_called = 0;

  // Sequence 4 (test)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunMessageLoop();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(2, cb_called);
  EXPECT_EQ(4, result_cb_called);
  EXPECT_TRUE(result);
  cb_called = 0;
  result_cb_called = 0;

  // Sequence 5 (test) (no callback passed to QueueCommand)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode));
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode));

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunMessageLoop();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(1, result_cb_called);
  EXPECT_TRUE(result);
}

TEST_F(SequentialCommandRunnerTest, SequentialCommandRunnerCancel) {
  auto command_bytes = common::CreateStaticByteBuffer(0xFF, 0xFF, 0x00);

  auto command_cmpl_error_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, Status::kHardwareFailure);

  auto command_cmpl_success_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, Status::kSuccess);

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

  test_device()->Start();

  bool result;
  int result_cb_called = 0;
  auto result_cb = [&, this](bool cb_result) {
    result = cb_result;
    result_cb_called++;
    message_loop()->QuitNow();
  };

  int cb_called = 0;
  auto cb = [&](const EventPacket& event) { cb_called++; };

  // Sequence 1: Sequence will be cancelled after the first command.
  SequentialCommandRunner cmd_runner(message_loop()->task_runner(),
                                     transport());
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  // Call RunCommands() and right away post a task to cancel the sequence. The
  // first command will go out but no successive packets should be sent and no
  // callbacks should be invoked.
  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  cmd_runner.Cancel();

  // Since |result_cb| is expected to not get called (which would normally quit
  // the message loop), we set a shorter-than-usual timeout for the message loop
  // here.
  RunMessageLoop(2);
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(0, result_cb_called);

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

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());

  // Since |result_cb| is expected to not get called (which would normally quit
  // the message loop), we set a shorter-than-usual timeout for the message loop
  // here.
  RunMessageLoop(2);
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(0, result_cb_called);

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
        cmd_runner.RunCommands(result_cb);
      });
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());

  RunMessageLoop();

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  // The result callback should have been called once with a failure result.
  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(1, result_cb_called);
  EXPECT_FALSE(result);
}

}  // namespace
}  // namespace hci
}  // namespace bluetooth
