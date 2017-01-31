// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/hci/command_channel.h"

#include <queue>
#include <thread>
#include <vector>

#include <magenta/status.h>
#include <mx/channel.h>

#include "gtest/gtest.h"

#include "apps/bluetooth/common/byte_buffer.h"
#include "apps/bluetooth/common/test_helpers.h"
#include "apps/bluetooth/hci/command_packet.h"
#include "apps/bluetooth/hci/hci.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"
#include "lib/mtl/threading/create_thread.h"

namespace bluetooth {
namespace hci {
namespace {

void NopStatusCallback(CommandChannel::TransactionId id, Status status) {}

#define NOP_STATUS_CB() \
  std::bind(&NopStatusCallback, std::placeholders::_1, std::placeholders::_2)

constexpr uint8_t UpperBits(const OpCode opcode) {
  return opcode >> 8;
}

constexpr uint8_t LowerBits(const OpCode opcode) {
  return opcode & 0x00FF;
}

constexpr uint8_t kNumHCICommandPackets = 1;

struct TestTransaction final {
  TestTransaction() = default;
  TestTransaction(common::ByteBuffer* expected,
                  const std::vector<common::ByteBuffer*>& responses) {
    this->expected = common::DynamicByteBuffer(expected->GetSize(),
                                               expected->TransferContents());
    for (auto* buffer : responses) {
      this->responses.push(common::DynamicByteBuffer(
          buffer->GetSize(), buffer->TransferContents()));
    }
  }

  TestTransaction(TestTransaction&& other) {
    expected = std::move(other.expected);
    responses = std::move(other.responses);
  }

  TestTransaction& operator=(TestTransaction&& other) {
    expected = std::move(other.expected);
    responses = std::move(other.responses);
    return *this;
  }

  common::DynamicByteBuffer expected;
  std::queue<common::DynamicByteBuffer> responses;

  FTL_DISALLOW_COPY_AND_ASSIGN(TestTransaction);
};

// Simple class that sits on one end of the HCI command channel and can be told
// to respond to received command packets using a pre-defined set of byte
// sequences.
class FakeController final : public ::mtl::MessageLoopHandler {
 public:
  // |channel|: The channel handle
  explicit FakeController(mx::channel channel) : channel_(std::move(channel)) {
    FTL_DCHECK(MX_HANDLE_INVALID != channel_.get());
  }

  ~FakeController() override { Stop(); }

  void Start(std::vector<TestTransaction>* transactions) {
    FTL_DCHECK(!transactions->empty());
    for (auto& t : *transactions) {
      transactions_.push(std::move(t));
    }
    thread_ = mtl::CreateThread(&task_runner_, "FakeController thread");
    task_runner_->PostTask([this] {
      key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
          this, channel_.get(), MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);
    });
  }

  void Stop() {
    if (task_runner_) {
      task_runner_->PostTask([this] {
        mtl::MessageLoop::GetCurrent()->RemoveHandler(key_);
        mtl::MessageLoop::GetCurrent()->QuitNow();
      });
    }
    if (thread_.joinable())
      thread_.join();
  }

 private:
  // ::mtl::MessageLoopHandler overrides:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override {
    ASSERT_EQ(handle, channel_.get());

    common::StaticByteBuffer<kMaxCommandPacketPayloadSize> buffer;
    uint32_t read_size;
    mx_status_t status =
        channel_.read(0u, buffer.GetMutableData(), kMaxCommandPacketPayloadSize,
                      &read_size, nullptr, 0, nullptr);
    ASSERT_TRUE(status == NO_ERROR || status == ERR_REMOTE_CLOSED);
    if (status < 0) {
      FTL_LOG(ERROR) << "Failed to read on channel: "
                     << mx_status_get_string(status);
      return;
    }

    ASSERT_FALSE(transactions_.empty());
    auto& current = transactions_.front();
    common::BufferView view(buffer.GetMutableData(), read_size);
    ASSERT_TRUE(ContainersEqual(current.expected, view));

    while (!current.responses.empty()) {
      auto& response = current.responses.front();
      status =
          channel_.write(0, response.GetData(), response.GetSize(), nullptr, 0);
      ASSERT_EQ(status, NO_ERROR)
          << "Failed to send response: " << mx_status_get_string(status);
      current.responses.pop();
    }

    transactions_.pop();
  }

  mx::channel channel_;
  std::queue<TestTransaction> transactions_;

  std::thread thread_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  mtl::MessageLoop::HandlerKey key_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakeController);
};

class CommandChannelTest : public ::testing::Test {
 public:
  CommandChannelTest() = default;
  ~CommandChannelTest() override = default;

 protected:
  // ::testing::Test overrides:
  void SetUp() override {
    mx::channel endpoint0, endpoint1;
    mx_status_t status = mx::channel::create(0, &endpoint0, &endpoint1);
    ASSERT_EQ(NO_ERROR, status);

    cmd_channel_ = std::make_unique<CommandChannel>(std::move(endpoint0));
    fake_controller_ = std::make_unique<FakeController>(std::move(endpoint1));

    cmd_channel_->Initialize();
  }

  void TearDown() override {
    cmd_channel_ = nullptr;
    fake_controller_ = nullptr;
  }

  void RunMessageLoop() {
    // Since we drive our tests using callbacks we set a time out here to
    // prevent the main loop from spinning forever in case of a failure.
    message_loop_.task_runner()->PostDelayedTask(
        [this] { message_loop_.QuitNow(); }, ftl::TimeDelta::FromSeconds(10));
    message_loop_.Run();
  }

  CommandChannel* cmd_channel() const { return cmd_channel_.get(); }
  FakeController* fake_controller() const { return fake_controller_.get(); }
  mtl::MessageLoop* message_loop() { return &message_loop_; }

 private:
  std::unique_ptr<CommandChannel> cmd_channel_;
  std::unique_ptr<FakeController> fake_controller_;
  mtl::MessageLoop message_loop_;
};

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
  std::vector<TestTransaction> transactions;
  transactions.push_back(TestTransaction(&req, {&rsp}));
  fake_controller()->Start(&transactions);

  // Send a HCI_Reset command.
  common::StaticByteBuffer<CommandPacket::GetMinBufferSize(0u)> buffer;
  CommandPacket reset(kReset, &buffer);
  reset.EncodeHeader();
  CommandChannel::TransactionId id = cmd_channel()->SendCommand(
      reset, NOP_STATUS_CB(),
      [&id, this](CommandChannel::TransactionId callback_id,
                  const EventPacket& event) {
        EXPECT_EQ(id, callback_id);
        EXPECT_EQ(kCommandCompleteEventCode, event.event_code());
        EXPECT_EQ(4, event.GetHeader().parameter_total_size);
        EXPECT_EQ(kNumHCICommandPackets,
                  event.GetPayload<CommandCompleteEventParams>()
                      ->num_hci_command_packets);
        EXPECT_EQ(kReset,
                  le16toh(event.GetPayload<CommandCompleteEventParams>()
                              ->command_opcode));
        EXPECT_EQ(Status::kHardwareFailure,
                  event.GetReturnParams<ResetReturnParams>()->status);

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
  std::vector<TestTransaction> transactions;
  transactions.push_back(TestTransaction(&req, {&rsp0, &rsp1}));
  fake_controller()->Start(&transactions);

  // Send HCI_Reset
  CommandChannel::TransactionId id;
  int status_cb_count = 0;
  auto status_cb = [&status_cb_count, &id](
      CommandChannel::TransactionId callback_id, Status status) {
    status_cb_count++;
    EXPECT_EQ(id, callback_id);
    EXPECT_EQ(Status::kSuccess, status);
  };
  auto complete_cb = [&id, this](CommandChannel::TransactionId callback_id,
                                 const EventPacket& event) {
    EXPECT_EQ(callback_id, id);
    EXPECT_EQ(kCommandCompleteEventCode, event.event_code());
    EXPECT_EQ(Status::kSuccess,
              event.GetReturnParams<ResetReturnParams>()->status);

    // Quit the message loop to continue the test.
    message_loop()->QuitNow();
  };

  common::StaticByteBuffer<CommandPacket::GetMinBufferSize(0u)> buffer;
  CommandPacket reset(kReset, &buffer);
  reset.EncodeHeader();
  id = cmd_channel()->SendCommand(reset, status_cb, complete_cb,
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
  std::vector<TestTransaction> transactions;
  transactions.push_back(TestTransaction(&req, {&rsp}));
  fake_controller()->Start(&transactions);

  // Send HCI_Reset
  CommandChannel::TransactionId id;
  int status_cb_count = 0;
  auto status_cb = [&status_cb_count, &id](
      CommandChannel::TransactionId callback_id, Status status) {
    status_cb_count++;
  };
  auto complete_cb = [&id, this](CommandChannel::TransactionId callback_id,
                                 const EventPacket& event) {
    EXPECT_EQ(callback_id, id);
    EXPECT_EQ(kCommandStatusEventCode, event.event_code());
    EXPECT_EQ(Status::kSuccess,
              event.GetPayload<CommandStatusEventParams>()->status);
    EXPECT_EQ(
        1,
        event.GetPayload<CommandStatusEventParams>()->num_hci_command_packets);
    EXPECT_EQ(
        kReset,
        le16toh(event.GetPayload<CommandStatusEventParams>()->command_opcode));

    // Quit the message loop to continue the test.
    message_loop()->QuitNow();
  };

  common::StaticByteBuffer<CommandPacket::GetMinBufferSize(0u)> buffer;
  CommandPacket reset(kReset, &buffer);
  reset.EncodeHeader();
  id = cmd_channel()->SendCommand(reset, status_cb, complete_cb,
                                  message_loop()->task_runner(),
                                  kCommandStatusEventCode);
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
  std::vector<TestTransaction> transactions;
  transactions.push_back(TestTransaction(&req0, {&rsp0}));
  transactions.push_back(TestTransaction(&req1, {&rsp1, &rsp2}));
  fake_controller()->Start(&transactions);

  // Begin transactions:
  CommandChannel::TransactionId id0, id1;
  int status_cb_count = 0;
  auto status_cb = [&status_cb_count, &id0, &id1](
      CommandChannel::TransactionId callback_id, Status status) {
    status_cb_count++;
    if (callback_id == id0) {
      EXPECT_EQ(Status::kHardwareFailure, status);
    } else {
      ASSERT_EQ(id1, callback_id);
      EXPECT_EQ(Status::kSuccess, status);
    }
  };
  int complete_cb_count = 0;
  auto complete_cb = [&id1, &complete_cb_count, this](
      CommandChannel::TransactionId callback_id, const EventPacket& event) {
    EXPECT_EQ(kCommandCompleteEventCode, event.event_code());
    complete_cb_count++;
    EXPECT_EQ(id1, callback_id);

    auto return_params = event.GetReturnParams<ReadBDADDRReturnParams>();
    EXPECT_EQ(Status::kSuccess, return_params->status);
    EXPECT_TRUE(common::ContainersEqual(
        std::array<uint8_t, 6>{{0x01, 0x02, 0x03, 0x04, 0x05, 0x06}},
        return_params->bd_addr, 6));

    // Quit the message loop to continue the test. We post a delayed task so
    // that our check for |complete_cb_count| == 1 isn't guaranteed to be true
    // because we quit the message loop.
    if (complete_cb_count == 1)
      message_loop()->PostQuitTask();
  };

  common::StaticByteBuffer<CommandPacket::GetMinBufferSize(0u)> buffer;
  CommandPacket reset(kReset, &buffer);
  reset.EncodeHeader();
  id0 = cmd_channel()->SendCommand(reset, status_cb, complete_cb,
                                   message_loop()->task_runner());
  CommandPacket read_bdaddr(kReadBDADDR, &buffer);
  read_bdaddr.EncodeHeader();
  id1 = cmd_channel()->SendCommand(read_bdaddr, status_cb, complete_cb,
                                   message_loop()->task_runner());
  RunMessageLoop();
  EXPECT_EQ(2, status_cb_count);
  EXPECT_EQ(1, complete_cb_count);
}

}  // namespace
}  // namespace hci
}  // namespace bluetooth
