// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_controller_base.h"

#include <zircon/status.h>

#include "apps/bluetooth/lib/common/run_task_sync.h"
#include "apps/bluetooth/lib/hci/acl_data_packet.h"
#include "apps/bluetooth/lib/hci/hci_constants.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fsl/threading/create_thread.h"

namespace bluetooth {
namespace testing {

FakeControllerBase::FakeControllerBase(zx::channel cmd_channel, zx::channel acl_data_channel)
    : cmd_channel_(std::move(cmd_channel)), acl_channel_(std::move(acl_data_channel)) {}

FakeControllerBase::~FakeControllerBase() {
  // When this destructor gets called any subclass state will be undefined. If Stop() has not been
  // called before reaching this point this can cause runtime errors when our MessageLoop handlers
  // attempt to invoke the pure virtual methods of this class. So we require that the FakeController
  // be stopped by now.
  FXL_DCHECK(!IsStarted());
}

void FakeControllerBase::Start() {
  FXL_DCHECK(!IsStarted());
  FXL_DCHECK(cmd_channel_.is_valid());
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  thread_ = fsl::CreateThread(&task_runner_, "bluetooth-hci-test-controller");

  auto setup_task = [this] {
    cmd_handler_key_ = fsl::MessageLoop::GetCurrent()->AddHandler(
        this, cmd_channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    if (acl_channel_.is_valid()) {
      acl_handler_key_ = fsl::MessageLoop::GetCurrent()->AddHandler(
          this, acl_channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    }
  };

  common::RunTaskSync(setup_task, task_runner_);
}

void FakeControllerBase::Stop() {
  FXL_DCHECK(IsStarted());
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  task_runner_->PostTask([this] {
    CloseCommandChannelInternal();
    CloseACLDataChannelInternal();
    fsl::MessageLoop::GetCurrent()->QuitNow();
  });
  if (thread_.joinable()) thread_.join();

  task_runner_ = nullptr;
}

void FakeControllerBase::SendCommandChannelPacket(const common::ByteBuffer& packet) {
  FXL_DCHECK(IsStarted());
  zx_status_t status = cmd_channel_.write(0, packet.data(), packet.size(), nullptr, 0);
  FXL_DCHECK(ZX_OK == status);
}

void FakeControllerBase::SendACLDataChannelPacket(const common::ByteBuffer& packet) {
  FXL_DCHECK(IsStarted());
  zx_status_t status = acl_channel_.write(0, packet.data(), packet.size(), nullptr, 0);
  FXL_DCHECK(ZX_OK == status);
}

void FakeControllerBase::CloseCommandChannel() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  common::RunTaskSync([this] { CloseCommandChannelInternal(); }, task_runner_);
}

void FakeControllerBase::CloseACLDataChannel() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  common::RunTaskSync([this] { CloseACLDataChannelInternal(); }, task_runner_);
}

void FakeControllerBase::OnHandleReady(zx_handle_t handle, zx_signals_t pending, uint64_t count) {
  if (handle == cmd_channel_.get()) {
    HandleCommandPacket();
  } else if (handle == acl_channel_.get()) {
    HandleACLPacket();
  }
}

void FakeControllerBase::HandleCommandPacket() {
  common::StaticByteBuffer<hci::kMaxCommandPacketPayloadSize> buffer;
  uint32_t read_size;
  zx_status_t status =
      cmd_channel_.read(0u, buffer.mutable_data(), hci::kMaxCommandPacketPayloadSize, &read_size,
                        nullptr, 0, nullptr);
  FXL_DCHECK(status == ZX_OK || status == ZX_ERR_PEER_CLOSED);
  if (status < 0) {
    if (status == ZX_ERR_PEER_CLOSED)
      FXL_LOG(INFO) << "Command channel was closed";
    else
      FXL_LOG(ERROR) << "Failed to read on cmd channel: " << zx_status_get_string(status);

    CloseCommandChannelInternal();
    return;
  }

  if (read_size < sizeof(hci::CommandHeader)) {
    FXL_LOG(ERROR) << "Malformed command packet received";
    return;
  }

  common::MutableBufferView view(buffer.mutable_data(), read_size);
  common::PacketView<hci::CommandHeader> packet(&view, read_size - sizeof(hci::CommandHeader));
  OnCommandPacketReceived(packet);
}

void FakeControllerBase::HandleACLPacket() {
  common::StaticByteBuffer<hci::kMaxACLPayloadSize + sizeof(hci::ACLDataHeader)> buffer;
  uint32_t read_size;
  zx_status_t status =
      acl_channel_.read(0u, buffer.mutable_data(), buffer.size(), &read_size, nullptr, 0, nullptr);
  FXL_DCHECK(status == ZX_OK || status == ZX_ERR_PEER_CLOSED);
  if (status < 0) {
    if (status == ZX_ERR_PEER_CLOSED)
      FXL_LOG(INFO) << "ACL channel was closed";
    else
      FXL_LOG(ERROR) << "Failed to read on ACL channel: " << zx_status_get_string(status);

    CloseACLDataChannelInternal();
    return;
  }

  common::BufferView view(buffer.data(), read_size);
  OnACLDataPacketReceived(view);
}

void FakeControllerBase::CloseCommandChannelInternal() {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  if (!cmd_handler_key_) return;

  fsl::MessageLoop::GetCurrent()->RemoveHandler(cmd_handler_key_);
  cmd_handler_key_ = 0u;
  cmd_channel_.reset();
}

void FakeControllerBase::CloseACLDataChannelInternal() {
  FXL_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  if (!acl_handler_key_) return;

  fsl::MessageLoop::GetCurrent()->RemoveHandler(acl_handler_key_);
  acl_handler_key_ = 0u;
  acl_channel_.reset();
}

}  // namespace testing
}  // namespace bluetooth
