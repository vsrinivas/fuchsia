// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_controller.h"

#include <magenta/status.h>

#include "apps/bluetooth/lib/common/test_helpers.h"
#include "apps/bluetooth/lib/hci/acl_data_packet.h"
#include "apps/bluetooth/lib/hci/hci_constants.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/threading/create_thread.h"

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

FakeController::FakeController(mx::channel cmd_channel, mx::channel acl_data_channel)
    : cmd_channel_(std::move(cmd_channel)), acl_channel_(std::move(acl_data_channel)) {
  FTL_DCHECK(cmd_channel_.is_valid());
}

FakeController::~FakeController() {
  Stop();
}

void FakeController::QueueCommandTransaction(CommandTransaction transaction) {
  cmd_transactions_.push(std::move(transaction));
}

void FakeController::Start() {
  thread_ = mtl::CreateThread(&task_runner_, "bluetooth-hci-test-controller");

  FTL_DCHECK(cmd_channel_.is_valid());

  // We make sure that this method blocks until the I/O handler registration task has run.
  std::mutex init_mutex;
  std::condition_variable init_cv;
  bool ready = false;

  task_runner_->PostTask([&] {
    cmd_handler_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
        this, cmd_channel_.get(), MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);
    if (acl_channel_.is_valid()) {
      acl_handler_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
          this, acl_channel_.get(), MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);
    }

    {
      std::lock_guard<std::mutex> lock(init_mutex);
      ready = true;
    }

    init_cv.notify_one();
  });

  std::unique_lock<std::mutex> lock(init_mutex);
  init_cv.wait(lock, [&ready] { return ready; });
}

void FakeController::Stop() {
  if (task_runner_) {
    task_runner_->PostTask([this] {
      mtl::MessageLoop::GetCurrent()->RemoveHandler(cmd_handler_key_);
      mtl::MessageLoop::GetCurrent()->RemoveHandler(acl_handler_key_);
      mtl::MessageLoop::GetCurrent()->QuitNow();
    });
  }
  if (thread_.joinable()) thread_.join();
}

void FakeController::SendCommandChannelPacket(const common::ByteBuffer& packet) {
  FTL_DCHECK(task_runner_.get());
  common::DynamicByteBuffer buffer(packet.GetSize(), packet.CopyContents());
  task_runner_->PostTask(ftl::MakeCopyable([ buffer = std::move(buffer), this ]() mutable {
    mx_status_t status = cmd_channel_.write(0, buffer.GetData(), buffer.GetSize(), nullptr, 0);
    ASSERT_EQ(NO_ERROR, status);
  }));
}

void FakeController::SendACLDataChannelPacket(const common::ByteBuffer& packet) {
  FTL_DCHECK(task_runner_.get());
  common::DynamicByteBuffer buffer(packet.GetSize(), packet.CopyContents());
  task_runner_->PostTask(ftl::MakeCopyable([ buffer = std::move(buffer), this ]() mutable {
    mx_status_t status = acl_channel_.write(0, buffer.GetData(), buffer.GetSize(), nullptr, 0);
    ASSERT_EQ(NO_ERROR, status);
  }));
}

void FakeController::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  if (handle == cmd_channel_.get()) {
    HandleCommandPacket();
  } else if (handle == acl_channel_.get()) {
    HandleACLPacket();
  }
}

void FakeController::SetDataCallback(const DataCallback& callback,
                                     ftl::RefPtr<ftl::TaskRunner> task_runner) {
  FTL_DCHECK(callback);
  FTL_DCHECK(task_runner);
  FTL_DCHECK(!data_callback_);
  FTL_DCHECK(!data_task_runner_);

  data_callback_ = callback;
  data_task_runner_ = task_runner;
}

void FakeController::CloseCommandChannel() {
  task_runner_->PostTask([this] {
    mtl::MessageLoop::GetCurrent()->RemoveHandler(cmd_handler_key_);
    cmd_channel_.reset();
  });
}

void FakeController::CloseACLDataChannel() {
  task_runner_->PostTask([this] {
    mtl::MessageLoop::GetCurrent()->RemoveHandler(acl_handler_key_);
    acl_channel_.reset();
  });
}

void FakeController::HandleCommandPacket() {
  common::StaticByteBuffer<kMaxCommandPacketPayloadSize> buffer;
  uint32_t read_size;
  mx_status_t status = cmd_channel_.read(0u, buffer.GetMutableData(), kMaxCommandPacketPayloadSize,
                                         &read_size, nullptr, 0, nullptr);
  ASSERT_TRUE(status == NO_ERROR || status == ERR_PEER_CLOSED);
  if (status < 0) {
    FTL_LOG(ERROR) << "Failed to read on cmd channel: " << mx_status_get_string(status);
    return;
  }

  ASSERT_FALSE(cmd_transactions_.empty());
  auto& current = cmd_transactions_.front();
  common::BufferView view(buffer.GetData(), read_size);
  ASSERT_TRUE(ContainersEqual(current.expected_, view));

  while (!current.replies_.empty()) {
    auto& reply = current.replies_.front();
    status = cmd_channel_.write(0, reply.GetData(), reply.GetSize(), nullptr, 0);
    ASSERT_EQ(NO_ERROR, status) << "Failed to send reply: " << mx_status_get_string(status);
    current.replies_.pop();
  }

  cmd_transactions_.pop();
}

void FakeController::HandleACLPacket() {
  if (!data_callback_) return;
  common::StaticByteBuffer<ACLDataTxPacket::GetMinBufferSize(kMaxACLPayloadSize)> buffer;
  uint32_t read_size;
  mx_status_t status = acl_channel_.read(0u, buffer.GetMutableData(), buffer.GetSize(), &read_size,
                                         nullptr, 0, nullptr);
  ASSERT_TRUE(status == NO_ERROR || status == ERR_PEER_CLOSED);
  if (status < 0) {
    FTL_LOG(ERROR) << "Failed to read on acl channel: " << mx_status_get_string(status);
    return;
  }

  // Invoke callback.
  common::DynamicByteBuffer cb_buffer(read_size);
  memcpy(cb_buffer.GetMutableData(), buffer.GetData(), read_size);

  data_task_runner_->PostTask(ftl::MakeCopyable(
      [ buffer = std::move(cb_buffer), cb = data_callback_ ]() mutable { cb(buffer); }));
}

}  // namespace test
}  // namespace hci
}  // namespace bluetooth
