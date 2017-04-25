// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/hci/command_channel.h"

#include <mx/channel.h>

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/hci/command_packet.h"
#include "apps/bluetooth/lib/hci/device_wrapper.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/hci/sequential_command_runner.h"
#include "apps/bluetooth/lib/hci/test_controller.h"
#include "apps/bluetooth/lib/hci/transport.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace bluetooth {
namespace hci {
namespace test {
namespace {

void NopStatusCallback(CommandChannel::TransactionId, Status) {}
void NopCompleteCallback(CommandChannel::TransactionId, const EventPacket&) {}

#define NOP_STATUS_CB() std::bind(&NopStatusCallback, std::placeholders::_1, std::placeholders::_2)

#define NOP_COMPLETE_CB() \
  std::bind(&NopCompleteCallback, std::placeholders::_1, std::placeholders::_2)

constexpr uint8_t UpperBits(const OpCode opcode) {
  return opcode >> 8;
}

constexpr uint8_t LowerBits(const OpCode opcode) {
  return opcode & 0x00FF;
}

constexpr uint8_t kNumHCICommandPackets = 1;

class CommandChannelTest : public ::testing::Test {
 public:
  CommandChannelTest() = default;
  ~CommandChannelTest() override = default;

 protected:
  // ::testing::Test overrides:
  void SetUp() override {
    mx::channel cmd0, cmd1;
    mx_status_t status = mx::channel::create(0, &cmd0, &cmd1);
    ASSERT_EQ(NO_ERROR, status);

    auto hci_dev = std::make_unique<DummyDeviceWrapper>(std::move(cmd0), mx::channel());
    transport_ = hci::Transport::Create(std::move(hci_dev));

    test_controller_ = std::make_unique<TestController>(std::move(cmd1), mx::channel());

    transport_->Initialize();
  }

  void TearDown() override {
    transport_ = nullptr;
    test_controller_ = nullptr;
  }

  void RunMessageLoop() {
    // Since we drive our tests using callbacks we set a time out here to
    // prevent the main loop from spinning forever in case of a failure.
    message_loop_.task_runner()->PostDelayedTask([this] { message_loop_.QuitNow(); },
                                                 ftl::TimeDelta::FromSeconds(10));
    message_loop_.Run();
  }

  ftl::RefPtr<Transport> transport() const { return transport_; }
  CommandChannel* cmd_channel() const { return transport_->command_channel(); }
  TestController* test_controller() const { return test_controller_.get(); }
  mtl::MessageLoop* message_loop() { return &message_loop_; }

 private:
  ftl::RefPtr<Transport> transport_;
  std::unique_ptr<TestController> test_controller_;
  mtl::MessageLoop message_loop_;
};

TEST_F(CommandChannelTest, CommandTimeout) {
  // Set up expectations:
  // HCI_Reset
  auto req = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );

  // No reply.
  test_controller()->QueueCommandTransaction(CommandTransaction(req, {}));
  test_controller()->Start();

  // Send a HCI_Reset command.
  CommandChannel::TransactionId last_id = 0;
  Status last_status = Status::kSuccess;
  auto status_cb = [&, this](CommandChannel::TransactionId id, Status status) {
    last_id = id;
    last_status = status;
    message_loop()->QuitNow();
  };

  common::StaticByteBuffer<CommandPacket::GetMinBufferSize(0u)> buffer;
  CommandPacket reset(kReset, &buffer);
  reset.EncodeHeader();
  CommandChannel::TransactionId id = cmd_channel()->SendCommand(
      common::DynamicByteBuffer(buffer), status_cb, NOP_COMPLETE_CB(),
      message_loop()->task_runner());

  RunMessageLoop();

  EXPECT_EQ(id, last_id);
  EXPECT_EQ(Status::kCommandTimeout, last_status);
}

TEST_F(CommandChannelTest, SingleRequestResponse) {
  // Set up expectations:
  // HCI_Reset
  auto req = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  // HCI_CommandComplete
  auto rsp = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      kNumHCICommandPackets, LowerBits(kReset),
      UpperBits(kReset),  // HCI_Reset opcode
      Status::kHardwareFailure);
  test_controller()->QueueCommandTransaction(CommandTransaction(req, {&rsp}));
  test_controller()->Start();

  // Send a HCI_Reset command.
  common::StaticByteBuffer<CommandPacket::GetMinBufferSize(0u)> buffer;
  CommandPacket reset(kReset, &buffer);
  reset.EncodeHeader();
  CommandChannel::TransactionId id = cmd_channel()->SendCommand(
      common::DynamicByteBuffer(buffer), NOP_STATUS_CB(),
      [&id, this](CommandChannel::TransactionId callback_id, const EventPacket& event) {
        EXPECT_EQ(id, callback_id);
        EXPECT_EQ(kCommandCompleteEventCode, event.event_code());
        EXPECT_EQ(4, event.GetHeader().parameter_total_size);
        EXPECT_EQ(kNumHCICommandPackets,
                  event.GetPayload<CommandCompleteEventParams>()->num_hci_command_packets);
        EXPECT_EQ(kReset, le16toh(event.GetPayload<CommandCompleteEventParams>()->command_opcode));
        EXPECT_EQ(Status::kHardwareFailure, event.GetReturnParams<SimpleReturnParams>()->status);

        // Quit the message loop to continue the test.
        message_loop()->QuitNow();
      },
      message_loop()->task_runner());
  RunMessageLoop();
}

TEST_F(CommandChannelTest, SingleRequestWithStatusResponse) {
  // Set up expectations:
  // HCI_Reset
  auto req = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  // HCI_CommandStatus
  auto rsp0 = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kSuccess, kNumHCICommandPackets, LowerBits(kReset),
      UpperBits(kReset)  // HCI_Reset opcode
      );
  // HCI_CommandComplete
  auto rsp1 = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      kNumHCICommandPackets, LowerBits(kReset),
      UpperBits(kReset),  // HCI_Reset opcode
      Status::kSuccess);
  test_controller()->QueueCommandTransaction(CommandTransaction(req, {&rsp0, &rsp1}));
  test_controller()->Start();

  // Send HCI_Reset
  CommandChannel::TransactionId id;
  int status_cb_count = 0;
  auto status_cb = [&status_cb_count, &id](CommandChannel::TransactionId callback_id,
                                           Status status) {
    status_cb_count++;
    EXPECT_EQ(id, callback_id);
    EXPECT_EQ(Status::kSuccess, status);
  };
  auto complete_cb = [&id, this](CommandChannel::TransactionId callback_id,
                                 const EventPacket& event) {
    EXPECT_EQ(callback_id, id);
    EXPECT_EQ(kCommandCompleteEventCode, event.event_code());
    EXPECT_EQ(Status::kSuccess, event.GetReturnParams<SimpleReturnParams>()->status);

    // Quit the message loop to continue the test.
    message_loop()->QuitNow();
  };

  common::StaticByteBuffer<CommandPacket::GetMinBufferSize(0u)> buffer;
  CommandPacket reset(kReset, &buffer);
  reset.EncodeHeader();
  id = cmd_channel()->SendCommand(common::DynamicByteBuffer(buffer), status_cb, complete_cb,
                                  message_loop()->task_runner());
  RunMessageLoop();
  EXPECT_EQ(1, status_cb_count);
}

TEST_F(CommandChannelTest, SingleRequestWithCustomResponse) {
  // Set up expectations
  // HCI_Reset for the sake of testing
  auto req = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  // HCI_CommandStatus
  auto rsp = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kSuccess, kNumHCICommandPackets, LowerBits(kReset),
      UpperBits(kReset)  // HCI_Reset opcode
      );
  test_controller()->QueueCommandTransaction(CommandTransaction(req, {&rsp}));
  test_controller()->Start();

  // Send HCI_Reset
  CommandChannel::TransactionId id;
  int status_cb_count = 0;
  auto status_cb = [&status_cb_count, &id](CommandChannel::TransactionId callback_id,
                                           Status status) { status_cb_count++; };
  auto complete_cb = [&id, this](CommandChannel::TransactionId callback_id,
                                 const EventPacket& event) {
    EXPECT_EQ(callback_id, id);
    EXPECT_EQ(kCommandStatusEventCode, event.event_code());
    EXPECT_EQ(Status::kSuccess, event.GetPayload<CommandStatusEventParams>()->status);
    EXPECT_EQ(1, event.GetPayload<CommandStatusEventParams>()->num_hci_command_packets);
    EXPECT_EQ(kReset, le16toh(event.GetPayload<CommandStatusEventParams>()->command_opcode));

    // Quit the message loop to continue the test.
    message_loop()->QuitNow();
  };

  common::StaticByteBuffer<CommandPacket::GetMinBufferSize(0u)> buffer;
  CommandPacket reset(kReset, &buffer);
  reset.EncodeHeader();
  id = cmd_channel()->SendCommand(common::DynamicByteBuffer(buffer), status_cb, complete_cb,
                                  message_loop()->task_runner(), kCommandStatusEventCode);
  RunMessageLoop();

  // |status_cb| shouldn't have been called since it was used as the completion
  // callback.
  EXPECT_EQ(0, status_cb_count);
}

TEST_F(CommandChannelTest, MultipleQueuedRequests) {
  // Set up expectations:
  // Transaction 1:
  // HCI_Reset
  auto req0 = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  // HCI_CommandStatus with error
  auto rsp0 = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kHardwareFailure, kNumHCICommandPackets, LowerBits(kReset),
      UpperBits(kReset)  // HCI_Reset opcode
      );
  // Transaction 2:
  // HCI_Read_BDADDR
  auto req1 = common::CreateStaticByteBuffer(
      LowerBits(kReadBDADDR), UpperBits(kReadBDADDR),  // HCI_Read_BD_ADDR
      0x00                                             // parameter_total_size
      );
  // HCI_CommandStatus
  auto rsp1 = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kSuccess, kNumHCICommandPackets, LowerBits(kReadBDADDR),
      UpperBits(kReadBDADDR));
  // HCI_CommandComplete
  auto rsp2 = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x0A,  // parameter_total_size (10 byte payload)
      kNumHCICommandPackets, LowerBits(kReadBDADDR), UpperBits(kReadBDADDR),
      Status::kSuccess, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06  // BD_ADDR
      );
  test_controller()->QueueCommandTransaction(CommandTransaction(req0, {&rsp0}));
  test_controller()->QueueCommandTransaction(CommandTransaction(req1, {&rsp1, &rsp2}));
  test_controller()->Start();

  // Begin transactions:
  CommandChannel::TransactionId id0, id1;
  int status_cb_count = 0;
  auto status_cb = [&status_cb_count, &id0, &id1](CommandChannel::TransactionId callback_id,
                                                  Status status) {
    status_cb_count++;
    if (callback_id == id0) {
      EXPECT_EQ(Status::kHardwareFailure, status);
    } else {
      ASSERT_EQ(id1, callback_id);
      EXPECT_EQ(Status::kSuccess, status);
    }
  };
  int complete_cb_count = 0;
  auto complete_cb = [&id1, &complete_cb_count, this](CommandChannel::TransactionId callback_id,
                                                      const EventPacket& event) {
    EXPECT_EQ(kCommandCompleteEventCode, event.event_code());
    complete_cb_count++;
    EXPECT_EQ(id1, callback_id);

    auto return_params = event.GetReturnParams<ReadBDADDRReturnParams>();
    EXPECT_EQ(Status::kSuccess, return_params->status);
    EXPECT_EQ("06:05:04:03:02:01", return_params->bd_addr.ToString());

    // Quit the message loop to continue the test. We post a delayed task so
    // that our check for |complete_cb_count| == 1 isn't guaranteed to be true
    // because we quit the message loop.
    if (complete_cb_count == 1) message_loop()->PostQuitTask();
  };

  common::StaticByteBuffer<CommandPacket::GetMinBufferSize(0u)> buffer;
  CommandPacket reset(kReset, &buffer);
  reset.EncodeHeader();
  id0 = cmd_channel()->SendCommand(common::DynamicByteBuffer(buffer), status_cb, complete_cb,
                                   message_loop()->task_runner());
  CommandPacket read_bdaddr(kReadBDADDR, &buffer);
  read_bdaddr.EncodeHeader();
  id1 = cmd_channel()->SendCommand(common::DynamicByteBuffer(buffer), status_cb, complete_cb,
                                   message_loop()->task_runner());
  RunMessageLoop();
  EXPECT_EQ(2, status_cb_count);
  EXPECT_EQ(1, complete_cb_count);
}

TEST_F(CommandChannelTest, EventHandlerBasic) {
  constexpr EventCode kTestEventCode0 = 0xFE;
  constexpr EventCode kTestEventCode1 = 0xFF;
  auto cmd_status =
      common::CreateStaticByteBuffer(kCommandStatusEventCode, 0x04, 0x00, 0x01, 0x00, 0x00);
  auto cmd_complete =
      common::CreateStaticByteBuffer(kCommandCompleteEventCode, 0x03, 0x01, 0x00, 0x00);
  auto event0 = common::CreateStaticByteBuffer(kTestEventCode0, 0x00);
  auto event1 = common::CreateStaticByteBuffer(kTestEventCode1, 0x00);

  int event_count0 = 0;
  auto event_cb0 = [&event_count0, kTestEventCode0](const EventPacket& event) {
    event_count0++;
    EXPECT_EQ(kTestEventCode0, event.event_code());
  };

  int event_count1 = 0;
  auto event_cb1 = [&event_count1, kTestEventCode1, this](const EventPacket& event) {
    event_count1++;
    EXPECT_EQ(kTestEventCode1, event.event_code());

    // The code below will send this event twice. Quit the message loop when we
    // get the second event.
    if (event_count1 == 2) message_loop()->PostQuitTask();
  };

  auto id0 =
      cmd_channel()->AddEventHandler(kTestEventCode0, event_cb0, message_loop()->task_runner());
  EXPECT_NE(0u, id0);

  // Cannot register a handler for the same event code more than once.
  auto id1 =
      cmd_channel()->AddEventHandler(kTestEventCode0, event_cb1, message_loop()->task_runner());
  EXPECT_EQ(0u, id1);

  // Add a handler for a different event code.
  id1 = cmd_channel()->AddEventHandler(kTestEventCode1, event_cb1, message_loop()->task_runner());
  EXPECT_NE(0u, id1);

  test_controller()->Start();
  test_controller()->SendCommandChannelPacket(cmd_status);
  test_controller()->SendCommandChannelPacket(cmd_complete);
  test_controller()->SendCommandChannelPacket(event1);
  test_controller()->SendCommandChannelPacket(event0);
  test_controller()->SendCommandChannelPacket(cmd_complete);
  test_controller()->SendCommandChannelPacket(event0);
  test_controller()->SendCommandChannelPacket(event0);
  test_controller()->SendCommandChannelPacket(cmd_status);
  test_controller()->SendCommandChannelPacket(event1);

  RunMessageLoop();

  EXPECT_EQ(3, event_count0);
  EXPECT_EQ(2, event_count1);

  event_count0 = 0;
  event_count1 = 0;

  // Remove the first event handler.
  cmd_channel()->RemoveEventHandler(id0);
  test_controller()->SendCommandChannelPacket(event0);
  test_controller()->SendCommandChannelPacket(event0);
  test_controller()->SendCommandChannelPacket(event0);
  test_controller()->SendCommandChannelPacket(event1);
  test_controller()->SendCommandChannelPacket(event0);
  test_controller()->SendCommandChannelPacket(event0);
  test_controller()->SendCommandChannelPacket(event0);
  test_controller()->SendCommandChannelPacket(event0);
  test_controller()->SendCommandChannelPacket(event1);

  RunMessageLoop();

  EXPECT_EQ(0, event_count0);
  EXPECT_EQ(2, event_count1);
}

TEST_F(CommandChannelTest, EventHandlerEventWhileTransactionPending) {
  // HCI_Reset
  auto req = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );

  auto cmd_status = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kSuccess, 0x01, LowerBits(kReset),
      UpperBits(kReset)  // HCI_Reset opcode
      );

  constexpr EventCode kTestEventCode = 0xFF;
  auto event0 = common::CreateStaticByteBuffer(kTestEventCode, 0x00);
  auto event1 = common::CreateStaticByteBuffer(kTestEventCode, 0x01, 0x00);

  // We will send the HCI_Reset command with kTestEventCode as the completion
  // event. The event handler we register below should only get invoked once and
  // after the pending transaction completes.
  test_controller()->QueueCommandTransaction(
      CommandTransaction(req, {&cmd_status, &event0, &event1}));
  test_controller()->Start();

  int event_count = 0;
  auto event_cb = [&event_count, kTestEventCode, this](const EventPacket& event) {
    event_count++;
    EXPECT_EQ(kTestEventCode, event.event_code());
    EXPECT_EQ(1, event.GetHeader().parameter_total_size);

    // We post this task to the end of the message queue so that the quit call
    // doesn't inherently guarantee that this callback gets invoked only once.
    message_loop()->PostQuitTask();
  };

  cmd_channel()->AddEventHandler(kTestEventCode, event_cb, message_loop()->task_runner());

  common::StaticByteBuffer<CommandPacket::GetMinBufferSize(0u)> buffer;
  CommandPacket reset(kReset, &buffer);
  reset.EncodeHeader();
  cmd_channel()->SendCommand(common::DynamicByteBuffer(buffer), NOP_STATUS_CB(), NOP_COMPLETE_CB(),
                             message_loop()->task_runner(), kTestEventCode);

  RunMessageLoop();

  EXPECT_EQ(1, event_count);
}

TEST_F(CommandChannelTest, LEMetaEventHandler) {
  constexpr EventCode kTestSubeventCode0 = 0xFE;
  constexpr EventCode kTestSubeventCode1 = 0xFF;
  auto le_meta_event_bytes0 =
      common::CreateStaticByteBuffer(hci::kLEMetaEventCode, 0x01, kTestSubeventCode0);
  auto le_meta_event_bytes1 =
      common::CreateStaticByteBuffer(hci::kLEMetaEventCode, 0x01, kTestSubeventCode1);

  int event_count0 = 0;
  auto event_cb0 = [&event_count0, kTestSubeventCode0, this](const EventPacket& event) {
    event_count0++;
    EXPECT_EQ(hci::kLEMetaEventCode, event.event_code());
    EXPECT_EQ(kTestSubeventCode0, event.GetPayload<LEMetaEventParams>()->subevent_code);
    message_loop()->PostQuitTask();
  };

  int event_count1 = 0;
  auto event_cb1 = [&event_count1, kTestSubeventCode1, this](const EventPacket& event) {
    event_count1++;
    EXPECT_EQ(hci::kLEMetaEventCode, event.event_code());
    EXPECT_EQ(kTestSubeventCode1, event.GetPayload<LEMetaEventParams>()->subevent_code);
    message_loop()->PostQuitTask();
  };

  auto id0 = cmd_channel()->AddLEMetaEventHandler(kTestSubeventCode0, event_cb0,
                                                  message_loop()->task_runner());
  EXPECT_NE(0u, id0);

  // Cannot register a handler for the same event code more than once.
  auto id1 = cmd_channel()->AddLEMetaEventHandler(kTestSubeventCode0, event_cb0,
                                                  message_loop()->task_runner());
  EXPECT_EQ(0u, id1);

  // Add a handle for a different event code.
  id1 = cmd_channel()->AddLEMetaEventHandler(kTestSubeventCode1, event_cb1,
                                             message_loop()->task_runner());
  EXPECT_NE(0u, id1);

  test_controller()->Start();

  test_controller()->SendCommandChannelPacket(le_meta_event_bytes0);
  RunMessageLoop();
  EXPECT_EQ(1, event_count0);
  EXPECT_EQ(0, event_count1);

  test_controller()->SendCommandChannelPacket(le_meta_event_bytes0);
  RunMessageLoop();
  EXPECT_EQ(2, event_count0);
  EXPECT_EQ(0, event_count1);

  test_controller()->SendCommandChannelPacket(le_meta_event_bytes1);
  RunMessageLoop();
  EXPECT_EQ(2, event_count0);
  EXPECT_EQ(1, event_count1);

  // Remove the first event handler.
  cmd_channel()->RemoveEventHandler(id0);
  test_controller()->SendCommandChannelPacket(le_meta_event_bytes0);
  test_controller()->SendCommandChannelPacket(le_meta_event_bytes1);
  RunMessageLoop();
  EXPECT_EQ(2, event_count0);
  EXPECT_EQ(2, event_count1);
}

TEST_F(CommandChannelTest, TransportClosedCallback) {
  test_controller()->Start();

  bool closed_cb_called = false;
  auto closed_cb = [&closed_cb_called, this] {
    closed_cb_called = true;
    message_loop()->QuitNow();
  };
  transport()->SetTransportClosedCallback(closed_cb, message_loop()->task_runner());

  message_loop()->task_runner()->PostTask([this] { test_controller()->CloseCommandChannel(); });
  RunMessageLoop();
  EXPECT_TRUE(closed_cb_called);
}

TEST_F(CommandChannelTest, SequentialCommandRunner) {
  // HCI_Reset
  auto reset_bytes = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  auto reset_status_error_bytes = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kHardwareFailure, kNumHCICommandPackets, LowerBits(kReset),
      UpperBits(kReset)  // HCI_Reset opcode
      );
  auto reset_cmpl_error_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      kNumHCICommandPackets, LowerBits(kReset),
      UpperBits(kReset),  // HCI_Reset opcode
      Status::kHardwareFailure);
  auto reset_cmpl_success_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      kNumHCICommandPackets, LowerBits(kReset),
      UpperBits(kReset),  // HCI_Reset opcode
      Status::kSuccess);

  // Here we perform multiple test sequences where we queue up several HCI_Reset commands in each
  // sequence. We expect each sequence to terminate differently after the following HCI
  // transactions:
  //
  // Sequence 1 (HCI packets)
  //    -> Reset; <- error status
  test_controller()->QueueCommandTransaction(
      CommandTransaction(reset_bytes, {&reset_status_error_bytes}));

  // Sequence 2 (HCI packets)
  //    -> Reset; <- error complete
  test_controller()->QueueCommandTransaction(
      CommandTransaction(reset_bytes, {&reset_cmpl_error_bytes}));

  // Sequence 3 (HCI packets)
  //    -> Reset; <- success complete
  //    -> Reset; <- error complete
  test_controller()->QueueCommandTransaction(
      CommandTransaction(reset_bytes, {&reset_cmpl_success_bytes}));
  test_controller()->QueueCommandTransaction(
      CommandTransaction(reset_bytes, {&reset_cmpl_error_bytes}));

  // Sequence 4 (HCI packets)
  //    -> Reset; <- success complete
  //    -> Reset; <- success complete
  test_controller()->QueueCommandTransaction(
      CommandTransaction(reset_bytes, {&reset_cmpl_success_bytes}));
  test_controller()->QueueCommandTransaction(
      CommandTransaction(reset_bytes, {&reset_cmpl_success_bytes}));

  // Sequence 5 (HCI packets)
  //    -> Reset; <- success complete
  //    -> Reset; <- success complete
  test_controller()->QueueCommandTransaction(
      CommandTransaction(reset_bytes, {&reset_cmpl_success_bytes}));
  test_controller()->QueueCommandTransaction(
      CommandTransaction(reset_bytes, {&reset_cmpl_success_bytes}));

  test_controller()->Start();

  bool result;
  auto result_cb = [&, this](bool cb_result) {
    result = cb_result;
    message_loop()->QuitNow();
  };

  int cb_called = 0;
  auto cb = [&](const EventPacket& event) { cb_called++; };

  // Sequence 1 (test)
  SequentialCommandRunner cmd_runner(message_loop()->task_runner(), transport());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  cmd_runner.QueueCommand(common::DynamicByteBuffer(reset_bytes), cb);
  cmd_runner.QueueCommand(common::DynamicByteBuffer(reset_bytes), cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunMessageLoop();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(0, cb_called);
  EXPECT_FALSE(result);

  // Sequence 2 (test)
  cmd_runner.QueueCommand(common::DynamicByteBuffer(reset_bytes), cb);
  cmd_runner.QueueCommand(common::DynamicByteBuffer(reset_bytes), cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunMessageLoop();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(0, cb_called);
  EXPECT_FALSE(result);

  // Sequence 3 (test)
  cmd_runner.QueueCommand(common::DynamicByteBuffer(reset_bytes), cb);
  cmd_runner.QueueCommand(common::DynamicByteBuffer(reset_bytes), cb);
  cmd_runner.QueueCommand(common::DynamicByteBuffer(reset_bytes), cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunMessageLoop();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(1, cb_called);
  EXPECT_FALSE(result);
  cb_called = 0;

  // Sequence 4 (test)
  cmd_runner.QueueCommand(common::DynamicByteBuffer(reset_bytes), cb);
  cmd_runner.QueueCommand(common::DynamicByteBuffer(reset_bytes), cb);

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunMessageLoop();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(2, cb_called);
  EXPECT_TRUE(result);
  cb_called = 0;

  // Sequence 5 (test) (no callback passed to QueueCommand)
  cmd_runner.QueueCommand(common::DynamicByteBuffer(reset_bytes));
  cmd_runner.QueueCommand(common::DynamicByteBuffer(reset_bytes));

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(result_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunMessageLoop();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(0, cb_called);
  EXPECT_TRUE(result);
}

}  // namespace
}  // namespace test
}  // namespace hci
}  // namespace bluetooth
