// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include <thread>
#include <vector>

#include <mx/channel.h>

#include "apps/bluetooth/common/byte_buffer.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace bluetooth {
namespace hci {
namespace test {

// A CommandTransaction is used to set up an expectation for a command channel packet and the
// events that should be sent back in response to it.
class CommandTransaction final {
 public:
  CommandTransaction() = default;
  CommandTransaction(const common::ByteBuffer& expected,
                     const std::vector<const common::ByteBuffer*>& replies);

  // Move constructor and assignment operator.
  CommandTransaction(CommandTransaction&& other) = default;
  CommandTransaction& operator=(CommandTransaction&& other) = default;

 private:
  friend class FakeController;

  bool HasMoreResponses() const;
  common::DynamicByteBuffer PopNextReply();

  common::DynamicByteBuffer expected_;
  std::queue<common::DynamicByteBuffer> replies_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CommandTransaction);
};

// FakeController provides stub channel endpoints for the HCI channels for unit tests.
class FakeController final : ::mtl::MessageLoopHandler {
 public:
  FakeController(mx::channel cmd_channel, mx::channel acl_data_channel);
  ~FakeController() override;

  // Queues a transaction into the FakeController's expected command queue. Each packet received
  // through the command channel endpoint will be verified against the next expected transaction in
  // the queue. A mismatch will cause a fatal assertion. On a match, FakeController will send back
  // the replies provided in the transaction.
  void QueueCommandTransaction(CommandTransaction transaction);

  // Kicks off the FakeController thread and message loop and starts processing transactions.
  // |debug_name| will be assigned as the name of the thread.
  void Start();

  // Stops the message loop and thread.
  void Stop();

  // Immediately sends the given packet over this FakeController's command channel endpoint.
  void SendCommandChannelPacket(const common::ByteBuffer& packet);

  // Immediately sends the given packet over this FakeController's ACL data channel endpoint.
  void SendACLDataChannelPacket(const common::ByteBuffer& packet);

  // Callback to invoke when a packet is received over the data channel.
  using DataCallback = std::function<void(const common::ByteBuffer& packet)>;
  void SetDataCallback(const DataCallback& callback, ftl::RefPtr<ftl::TaskRunner> task_runner);

  // Immediately closes the command channel endpoint.
  void CloseCommandChannel();

  // Immediately closes the ACL data channel endpoint.
  void CloseACLDataChannel();

 private:
  // ::mtl::MessageLoopHandler overrides
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;

  // Handle packets received over the channels.
  void HandleCommandPacket();
  void HandleACLPacket();

  mx::channel cmd_channel_;
  mx::channel acl_channel_;
  std::thread thread_;
  std::queue<CommandTransaction> cmd_transactions_;
  DataCallback data_callback_;
  ftl::RefPtr<ftl::TaskRunner> data_task_runner_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  mtl::MessageLoop::HandlerKey cmd_handler_key_;
  mtl::MessageLoop::HandlerKey acl_handler_key_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakeController);
};

}  // namespace test
}  // namespace hci
}  // namespace bluetooth
