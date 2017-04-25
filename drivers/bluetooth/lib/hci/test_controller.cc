// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_controller.h"

#include <magenta/status.h>

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/common/test_helpers.h"
#include "apps/bluetooth/lib/hci/command_packet.h"
#include "lib/ftl/functional/make_copyable.h"

namespace bluetooth {
namespace hci {
namespace test {

CommandTransaction::CommandTransaction(const common::ByteBuffer& expected,
                                       const std::vector<const common::ByteBuffer*>& replies)
    : expected_(expected.GetSize(), expected.CopyContents()) {
  for (const auto* buffer : replies) {
    replies_.push(common::DynamicByteBuffer(buffer->GetSize(), buffer->CopyContents()));
  }
}

bool CommandTransaction::HasMoreResponses() const {
  return !replies_.empty();
}

common::DynamicByteBuffer CommandTransaction::PopNextReply() {
  FTL_DCHECK(HasMoreResponses());
  auto reply = std::move(replies_.front());
  replies_.pop();
  return reply;
}

TestController::TestController(mx::channel cmd_channel, mx::channel acl_data_channel)
    : FakeControllerBase(std::move(cmd_channel), std::move(acl_data_channel)) {}

TestController::~TestController() {
  if (IsStarted()) Stop();
}

void TestController::QueueCommandTransaction(CommandTransaction transaction) {
  cmd_transactions_.push(std::move(transaction));
}

void TestController::SetDataCallback(const DataCallback& callback,
                                     ftl::RefPtr<ftl::TaskRunner> task_runner) {
  FTL_DCHECK(callback);
  FTL_DCHECK(task_runner);
  FTL_DCHECK(!data_callback_);
  FTL_DCHECK(!data_task_runner_);

  data_callback_ = callback;
  data_task_runner_ = task_runner;
}

void TestController::OnCommandPacketReceived(const CommandPacket& command_packet) {
  ASSERT_FALSE(cmd_transactions_.empty()) << "Received unexpected command packet";

  auto& current = cmd_transactions_.front();
  ASSERT_TRUE(ContainersEqual(current.expected_, *command_packet.buffer()));

  while (!current.replies_.empty()) {
    auto& reply = current.replies_.front();
    mx_status_t status = command_channel().write(0, reply.GetData(), reply.GetSize(), nullptr, 0);
    ASSERT_EQ(NO_ERROR, status) << "Failed to send reply: " << mx_status_get_string(status);
    current.replies_.pop();
  }

  cmd_transactions_.pop();
}

void TestController::OnACLDataPacketReceived(const common::ByteBuffer& acl_data_packet) {
  if (!data_callback_) return;

  common::DynamicByteBuffer packet_copy(acl_data_packet);
  data_task_runner_->PostTask(
      ftl::MakeCopyable([ packet_copy = std::move(packet_copy), cb = data_callback_ ]() mutable {
        cb(packet_copy);
      }));
}

}  // namespace test
}  // namespace hci
}  // namespace bluetooth
