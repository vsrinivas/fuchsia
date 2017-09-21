// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/testing/test_base.h"
#include "garnet/drivers/bluetooth/lib/testing/test_controller.h"

namespace bluetooth {
namespace hci {
namespace {

using ::bluetooth::testing::CommandTransaction;

using TestingBase = ::bluetooth::testing::TransportTest<::bluetooth::testing::TestController>;

constexpr uint8_t UpperBits(const OpCode opcode) {
  return opcode >> 8;
}

constexpr uint8_t LowerBits(const OpCode opcode) {
  return opcode & 0x00FF;
}

constexpr uint8_t kNumHCICommandPackets = 1;

// A reference counted object used to verify that HCI command completion and status callbacks are
// properly cleaned up after the end of a transaction.
class TestCallbackObject : public fxl::RefCountedThreadSafe<TestCallbackObject> {
 public:
  explicit TestCallbackObject(const fxl::Closure& deletion_callback)
      : deletion_cb_(deletion_callback) {}

  virtual ~TestCallbackObject() { deletion_cb_(); }

 private:
  fxl::Closure deletion_cb_;
};

class CommandChannelTest : public TestingBase {
 public:
  CommandChannelTest() = default;
  ~CommandChannelTest() override = default;
};

TEST_F(CommandChannelTest, CommandTimeout) {
  // Set up expectations:
  // clang-format off
  // HCI_Reset
  auto req = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  // clang-format on

  // No reply.
  test_device()->QueueCommandTransaction(CommandTransaction(req, {}));
  test_device()->Start();

  // Send a HCI_Reset command.
  CommandChannel::TransactionId last_id = 0;
  Status last_status = Status::kSuccess;
  auto status_cb = [&, this](CommandChannel::TransactionId id, Status status) {
    last_id = id;
    last_status = status;
    message_loop()->QuitNow();
  };

  auto reset = CommandPacket::New(kReset);
  CommandChannel::TransactionId id = cmd_channel()->SendCommand(
      std::move(reset), message_loop()->task_runner(), nullptr, status_cb);

  RunMessageLoop();

  EXPECT_EQ(id, last_id);
  EXPECT_EQ(Status::kCommandTimeout, last_status);
}

TEST_F(CommandChannelTest, SingleRequestResponse) {
  // Set up expectations:
  // clang-format off
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
  // clang-format on
  test_device()->QueueCommandTransaction(CommandTransaction(req, {&rsp}));
  test_device()->Start();

  // Send a HCI_Reset command. We attach an instance of TestCallbackObject to the callbacks to
  // verify that it gets cleaned up as expected.
  bool test_obj_deleted = false;
  auto test_obj =
      fxl::MakeRefCounted<TestCallbackObject>([&test_obj_deleted] { test_obj_deleted = true; });

  auto reset = CommandPacket::New(kReset);
  CommandChannel::TransactionId id = cmd_channel()->SendCommand(
      std::move(reset), message_loop()->task_runner(),
      [&id, this, test_obj](CommandChannel::TransactionId callback_id, const EventPacket& event) {
        EXPECT_EQ(id, callback_id);
        EXPECT_EQ(kCommandCompleteEventCode, event.event_code());
        EXPECT_EQ(4, event.view().header().parameter_total_size);
        EXPECT_EQ(kNumHCICommandPackets,
                  event.view().payload<CommandCompleteEventParams>().num_hci_command_packets);
        EXPECT_EQ(kReset,
                  le16toh(event.view().payload<CommandCompleteEventParams>().command_opcode));
        EXPECT_EQ(Status::kHardwareFailure, event.return_params<SimpleReturnParams>()->status);

        // Quit the message loop to continue the test.
        message_loop()->QuitNow();
      });

  test_obj = nullptr;
  EXPECT_FALSE(test_obj_deleted);
  RunMessageLoop();

  // Make sure that the I/O thread is no longer holding on to |test_obj|.
  TearDown();

  EXPECT_TRUE(test_obj_deleted);
}

TEST_F(CommandChannelTest, SingleRequestWithStatusResponse) {
  // Set up expectations:
  // clang-format off
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
  // clang-format on
  test_device()->QueueCommandTransaction(CommandTransaction(req, {&rsp0, &rsp1}));
  test_device()->Start();

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
    EXPECT_EQ(Status::kSuccess, event.return_params<SimpleReturnParams>()->status);

    // Quit the message loop to continue the test.
    message_loop()->QuitNow();
  };

  auto reset = CommandPacket::New(kReset);
  id = cmd_channel()->SendCommand(std::move(reset), message_loop()->task_runner(), complete_cb,
                                  status_cb);
  RunMessageLoop();
  EXPECT_EQ(1, status_cb_count);
}

TEST_F(CommandChannelTest, SingleRequestWithCustomResponse) {
  // Set up expectations
  // clang-format off
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
  // clang-format on
  test_device()->QueueCommandTransaction(CommandTransaction(req, {&rsp}));
  test_device()->Start();

  // Send HCI_Reset
  CommandChannel::TransactionId id;
  int status_cb_count = 0;
  auto status_cb = [&status_cb_count](CommandChannel::TransactionId callback_id,
                                           Status status) { status_cb_count++; };
  auto complete_cb = [&id, this](CommandChannel::TransactionId callback_id,
                                 const EventPacket& event) {
    EXPECT_EQ(callback_id, id);
    EXPECT_EQ(kCommandStatusEventCode, event.event_code());
    EXPECT_EQ(Status::kSuccess, event.view().payload<CommandStatusEventParams>().status);
    EXPECT_EQ(1, event.view().payload<CommandStatusEventParams>().num_hci_command_packets);
    EXPECT_EQ(kReset, le16toh(event.view().payload<CommandStatusEventParams>().command_opcode));

    // Quit the message loop to continue the test.
    message_loop()->QuitNow();
  };

  auto reset = CommandPacket::New(kReset);
  id = cmd_channel()->SendCommand(std::move(reset), message_loop()->task_runner(), complete_cb,
                                  status_cb, kCommandStatusEventCode);
  RunMessageLoop();

  // |status_cb| shouldn't have been called since it was used as the completion
  // callback.
  EXPECT_EQ(0, status_cb_count);
}

TEST_F(CommandChannelTest, SingleRequestWithCustomResponseAndMatcher) {
  constexpr EventCode kTestEventCode = 0xFF;
  constexpr size_t kExpectedEventCount = 3;
  constexpr size_t kExpectedCommandCompleteCount = 1;

  // Set up expectations
  // clang-format off
  // HCI_Reset for the sake of testing
  auto req = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  // clang-format on

  // Custom completion events. We send two events that match the matcher below and two events that
  // do not. We expect |rsp1| to complete the HCI command while the rest to be routed to the event
  // handler.
  auto rsp0 = common::CreateStaticByteBuffer(kTestEventCode, 0x00);
  auto rsp1 = common::CreateStaticByteBuffer(kTestEventCode, 0x01, 0x00);
  auto rsp2 = common::CreateStaticByteBuffer(kTestEventCode, 0x01, 0x00);
  auto rsp3 = common::CreateStaticByteBuffer(kTestEventCode, 0x00);

  test_device()->QueueCommandTransaction(CommandTransaction(req, {&rsp0, &rsp1, &rsp2, &rsp3}));
  test_device()->Start();

  // Send HCI_Reset
  CommandChannel::TransactionId id;
  unsigned int complete_cb_count = 0;
  auto complete_cb = [&id, &complete_cb_count, kTestEventCode](
                         CommandChannel::TransactionId callback_id, const EventPacket& event) {
    EXPECT_EQ(callback_id, id);
    EXPECT_EQ(kTestEventCode, event.event_code());
    EXPECT_EQ(1u, event.view().payload_size());

    complete_cb_count++;
  };

  unsigned int event_count_payload0 = 0;
  unsigned int event_count_payload1 = 0;
  auto event_cb = [&event_count_payload0, &event_count_payload1, kTestEventCode,
                   this](const EventPacket& event) {
    EXPECT_EQ(kTestEventCode, event.event_code());

    if (event.view().payload_size() == 0u)
      event_count_payload0++;
    else if (event.view().payload_size() == 1u)
      event_count_payload1++;

    // Quit the message loop to continue the test after we reach the expected event count.
    if (event_count_payload0 + event_count_payload1 < kExpectedEventCount) return;

    message_loop()->PostQuitTask();
  };
  auto event_handler_id =
      cmd_channel()->AddEventHandler(kTestEventCode, event_cb, message_loop()->task_runner());
  ASSERT_NE(0u, event_handler_id);

  // Below we send a Reset command with a custom completion event code and matcher. The matcher is
  // set up to match only one of the events that we sent above.
  auto matcher = [](const EventPacket& event) -> bool {
    // This will match |rsp1| and |rsp2| above (but should never be called on |rsp2| since the
    // command should complete before then).
    return event.view().payload_size() == 1;
  };

  auto reset = CommandPacket::New(kReset);
  id = cmd_channel()->SendCommand(std::move(reset), message_loop()->task_runner(), complete_cb,
                                  nullptr, kTestEventCode, matcher);
  RunMessageLoop();

  // |status_cb| shouldn't have been called since it was used as the completion
  // callback.
  EXPECT_EQ(2u, event_count_payload0);
  EXPECT_EQ(1u, event_count_payload1);
  EXPECT_EQ(kExpectedCommandCompleteCount, complete_cb_count);

  cmd_channel()->RemoveEventHandler(event_handler_id);
}

TEST_F(CommandChannelTest, MultipleQueuedRequests) {
  // Set up expectations:
  // clang-format off
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
  // clang-format on
  test_device()->QueueCommandTransaction(CommandTransaction(req0, {&rsp0}));
  test_device()->QueueCommandTransaction(CommandTransaction(req1, {&rsp1, &rsp2}));
  test_device()->Start();

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

    auto return_params = event.return_params<ReadBDADDRReturnParams>();
    EXPECT_EQ(Status::kSuccess, return_params->status);
    EXPECT_EQ("06:05:04:03:02:01", return_params->bd_addr.ToString());

    // Quit the message loop to continue the test. We post a delayed task so
    // that our check for |complete_cb_count| == 1 isn't guaranteed to be true
    // because we quit the message loop.
    if (complete_cb_count == 1) message_loop()->PostQuitTask();
  };

  auto reset = CommandPacket::New(kReset);
  id0 = cmd_channel()->SendCommand(std::move(reset), message_loop()->task_runner(), complete_cb,
                                   status_cb);
  auto read_bdaddr = CommandPacket::New(kReadBDADDR);
  id1 = cmd_channel()->SendCommand(std::move(read_bdaddr), message_loop()->task_runner(),
                                   complete_cb, status_cb);
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

  test_device()->Start();
  test_device()->SendCommandChannelPacket(cmd_status);
  test_device()->SendCommandChannelPacket(cmd_complete);
  test_device()->SendCommandChannelPacket(event1);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(cmd_complete);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(cmd_status);
  test_device()->SendCommandChannelPacket(event1);

  RunMessageLoop();

  EXPECT_EQ(3, event_count0);
  EXPECT_EQ(2, event_count1);

  event_count0 = 0;
  event_count1 = 0;

  // Remove the first event handler.
  cmd_channel()->RemoveEventHandler(id0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event1);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event1);

  RunMessageLoop();

  EXPECT_EQ(0, event_count0);
  EXPECT_EQ(2, event_count1);
}

TEST_F(CommandChannelTest, EventHandlerEventWhileTransactionPending) {
  // clang-format off
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
  // clang-format on

  constexpr EventCode kTestEventCode = 0xFF;
  auto event0 = common::CreateStaticByteBuffer(kTestEventCode, 0x00);
  auto event1 = common::CreateStaticByteBuffer(kTestEventCode, 0x01, 0x00);

  // We will send the HCI_Reset command with kTestEventCode as the completion
  // event. The event handler we register below should only get invoked once and
  // after the pending transaction completes.
  test_device()->QueueCommandTransaction(CommandTransaction(req, {&cmd_status, &event0, &event1}));
  test_device()->Start();

  int event_count = 0;
  auto event_cb = [&event_count, kTestEventCode, this](const EventPacket& event) {
    event_count++;
    EXPECT_EQ(kTestEventCode, event.event_code());
    EXPECT_EQ(1u, event.view().header().parameter_total_size);
    EXPECT_EQ(1u, event.view().payload_size());

    // We post this task to the end of the message queue so that the quit call
    // doesn't inherently guarantee that this callback gets invoked only once.
    message_loop()->PostQuitTask();
  };

  cmd_channel()->AddEventHandler(kTestEventCode, event_cb, message_loop()->task_runner());

  auto reset = CommandPacket::New(kReset);
  cmd_channel()->SendCommand(std::move(reset), message_loop()->task_runner(), nullptr, nullptr,
                             kTestEventCode);

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
    EXPECT_EQ(kTestSubeventCode0, event.view().payload<LEMetaEventParams>().subevent_code);
    message_loop()->PostQuitTask();
  };

  int event_count1 = 0;
  auto event_cb1 = [&event_count1, kTestSubeventCode1, this](const EventPacket& event) {
    event_count1++;
    EXPECT_EQ(hci::kLEMetaEventCode, event.event_code());
    EXPECT_EQ(kTestSubeventCode1, event.view().payload<LEMetaEventParams>().subevent_code);
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

  test_device()->Start();

  test_device()->SendCommandChannelPacket(le_meta_event_bytes0);
  RunMessageLoop();
  EXPECT_EQ(1, event_count0);
  EXPECT_EQ(0, event_count1);

  test_device()->SendCommandChannelPacket(le_meta_event_bytes0);
  RunMessageLoop();
  EXPECT_EQ(2, event_count0);
  EXPECT_EQ(0, event_count1);

  test_device()->SendCommandChannelPacket(le_meta_event_bytes1);
  RunMessageLoop();
  EXPECT_EQ(2, event_count0);
  EXPECT_EQ(1, event_count1);

  // Remove the first event handler.
  cmd_channel()->RemoveEventHandler(id0);
  test_device()->SendCommandChannelPacket(le_meta_event_bytes0);
  test_device()->SendCommandChannelPacket(le_meta_event_bytes1);
  RunMessageLoop();
  EXPECT_EQ(2, event_count0);
  EXPECT_EQ(2, event_count1);
}

TEST_F(CommandChannelTest, EventHandlerIdsDontCollide) {
  // Add a LE Meta event handler and a event handler and make sure that IDs are generated correctly
  // across the two methods.
  EXPECT_EQ(
      1u, cmd_channel()->AddLEMetaEventHandler(hci::kLEConnectionCompleteSubeventCode,
                                               [](const auto&) {}, message_loop()->task_runner()));
  EXPECT_EQ(2u, cmd_channel()->AddEventHandler(hci::kDisconnectionCompleteEventCode,
                                               [](const auto&) {}, message_loop()->task_runner()));
}

TEST_F(CommandChannelTest, TransportClosedCallback) {
  test_device()->Start();

  bool closed_cb_called = false;
  auto closed_cb = [&closed_cb_called, this] {
    closed_cb_called = true;
    message_loop()->QuitNow();
  };
  transport()->SetTransportClosedCallback(closed_cb, message_loop()->task_runner());

  message_loop()->task_runner()->PostTask([this] { test_device()->CloseCommandChannel(); });
  RunMessageLoop();
  EXPECT_TRUE(closed_cb_called);
}

}  // namespace
}  // namespace hci
}  // namespace bluetooth
