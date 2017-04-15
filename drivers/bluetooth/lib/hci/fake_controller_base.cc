// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_controller_base.h"

#include <magenta/status.h>

#include "apps/bluetooth/lib/hci/acl_data_packet.h"
#include "apps/bluetooth/lib/hci/command_packet.h"
#include "apps/bluetooth/lib/hci/hci_constants.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/threading/create_thread.h"

namespace bluetooth {
namespace hci {
namespace test {

FakeControllerBase::FakeControllerBase(mx::channel cmd_channel, mx::channel acl_data_channel)
    : cmd_channel_(std::move(cmd_channel)), acl_channel_(std::move(acl_data_channel)) {}

FakeControllerBase::~FakeControllerBase() {
  // When this destructor gets called any subclass state will be undefined. If Stop() has not been
  // called before reaching this point this can cause runtime errors when our MessageLoop handlers
  // attempt to invoke the pure virtual methods of this class. So we require that the FakeController
  // be stopped by now.
  FTL_DCHECK(!IsStarted());
}

void FakeControllerBase::Start() {
  FTL_DCHECK(!IsStarted());
  FTL_DCHECK(cmd_channel_.is_valid());
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  thread_ = mtl::CreateThread(&task_runner_, "bluetooth-hci-test-controller");

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

void FakeControllerBase::Stop() {
  FTL_DCHECK(IsStarted());
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  task_runner_->PostTask([this] {
    mtl::MessageLoop::GetCurrent()->RemoveHandler(cmd_handler_key_);
    mtl::MessageLoop::GetCurrent()->RemoveHandler(acl_handler_key_);
    mtl::MessageLoop::GetCurrent()->QuitNow();
  });
  if (thread_.joinable()) thread_.join();

  task_runner_ = nullptr;
}

void FakeControllerBase::SendCommandChannelPacket(const common::ByteBuffer& packet) {
  FTL_DCHECK(IsStarted());
  mx_status_t status = cmd_channel_.write(0, packet.GetData(), packet.GetSize(), nullptr, 0);
  FTL_DCHECK(NO_ERROR == status);
}

void FakeControllerBase::SendACLDataChannelPacket(const common::ByteBuffer& packet) {
  FTL_DCHECK(IsStarted());
  mx_status_t status = acl_channel_.write(0, packet.GetData(), packet.GetSize(), nullptr, 0);
  FTL_DCHECK(NO_ERROR == status);
}

void FakeControllerBase::CloseCommandChannel() {
  cmd_channel_.reset();
}

void FakeControllerBase::CloseACLDataChannel() {
  acl_channel_.reset();
}

void FakeControllerBase::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  if (handle == cmd_channel_.get()) {
    HandleCommandPacket();
  } else if (handle == acl_channel_.get()) {
    HandleACLPacket();
  }
}

void FakeControllerBase::HandleCommandPacket() {
  common::StaticByteBuffer<kMaxCommandPacketPayloadSize> buffer;
  uint32_t read_size;
  mx_status_t status = cmd_channel_.read(0u, buffer.GetMutableData(), kMaxCommandPacketPayloadSize,
                                         &read_size, nullptr, 0, nullptr);
  FTL_DCHECK(status == NO_ERROR || status == ERR_PEER_CLOSED);
  if (status < 0) {
    if (status == ERR_PEER_CLOSED)
      FTL_LOG(INFO) << "Command channel was closed";
    else
      FTL_LOG(ERROR) << "Failed to read on cmd channel: " << mx_status_get_string(status);

    mtl::MessageLoop::GetCurrent()->RemoveHandler(cmd_handler_key_);
    return;
  }

  if (read_size < hci::CommandPacket::GetMinBufferSize(0u)) {
    FTL_LOG(ERROR) << "Malformed command packet received";
    return;
  }

  common::MutableBufferView view(buffer.GetMutableData(), read_size);
  hci::CommandPacket packet(&view);
  OnCommandPacketReceived(packet);
}

void FakeControllerBase::HandleACLPacket() {
  common::StaticByteBuffer<ACLDataTxPacket::GetMinBufferSize(kMaxACLPayloadSize)> buffer;
  uint32_t read_size;
  mx_status_t status = acl_channel_.read(0u, buffer.GetMutableData(), buffer.GetSize(), &read_size,
                                         nullptr, 0, nullptr);
  FTL_DCHECK(status == NO_ERROR || status == ERR_PEER_CLOSED);
  if (status < 0) {
    if (status == ERR_PEER_CLOSED)
      FTL_LOG(INFO) << "ACL channel was closed";
    else
      FTL_LOG(ERROR) << "Failed to read on ACL channel: " << mx_status_get_string(status);

    mtl::MessageLoop::GetCurrent()->RemoveHandler(acl_handler_key_);
    return;
  }

  common::BufferView view(buffer.GetData(), read_size);
  OnACLDataPacketReceived(view);
}

}  // namespace test
}  // namespace hci
}  // namespace bluetooth
