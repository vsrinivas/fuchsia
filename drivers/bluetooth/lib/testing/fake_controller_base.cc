// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_controller_base.h"

#include <lib/async/default.h>
#include <zircon/status.h>

#include "garnet/drivers/bluetooth/lib/hci/acl_data_packet.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"

namespace btlib {
namespace testing {

FakeControllerBase::FakeControllerBase() {}

FakeControllerBase::~FakeControllerBase() {
  // When this destructor gets called any subclass state will be undefined. If
  // Stop() has not been called before reaching this point this can cause
  // runtime errors when our event loop handlers attempt to invoke the pure
  // virtual methods of this class.
}

bool FakeControllerBase::StartCmdChannel(zx::channel chan) {
  if (cmd_channel_.is_valid()) {
    return false;
  }

  cmd_channel_ = std::move(chan);
  cmd_channel_wait_.set_object(cmd_channel_.get());
  cmd_channel_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  zx_status_t status = cmd_channel_wait_.Begin(async_get_default_dispatcher());
  if (status != ZX_OK) {
    cmd_channel_.reset();
    FXL_LOG(WARNING) << "FakeController: Failed to Start Command channel: "
                     << zx_status_get_string(status);
    return false;
  }
  return true;
}

bool FakeControllerBase::StartAclChannel(zx::channel chan) {
  if (acl_channel_.is_valid()) {
    return false;
  }

  acl_channel_ = std::move(chan);
  acl_channel_wait_.set_object(acl_channel_.get());
  acl_channel_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  zx_status_t status = acl_channel_wait_.Begin(async_get_default_dispatcher());
  if (status != ZX_OK) {
    acl_channel_.reset();
    FXL_LOG(WARNING) << "FakeController: Failed to Start ACL channel: "
                     << zx_status_get_string(status);
    return false;
  }
  return true;
}

void FakeControllerBase::Stop() {
  CloseCommandChannel();
  CloseACLDataChannel();
}

zx_status_t FakeControllerBase::SendCommandChannelPacket(
    const common::ByteBuffer& packet) {
  zx_status_t status =
      cmd_channel_.write(0, packet.data(), packet.size(), nullptr, 0);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "FakeController: Failed to write to control channel: "
                     << zx_status_get_string(status);
  }
  return status;
}

zx_status_t FakeControllerBase::SendACLDataChannelPacket(
    const common::ByteBuffer& packet) {
  zx_status_t status =
      acl_channel_.write(0, packet.data(), packet.size(), nullptr, 0);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "FakeController: Failed to write to ACL data channel: "
                     << zx_status_get_string(status);
  }
  return status;
}

void FakeControllerBase::CloseCommandChannel() {
  if (cmd_channel_.is_valid()) {
    cmd_channel_wait_.Cancel();
    cmd_channel_wait_.set_object(ZX_HANDLE_INVALID);
    cmd_channel_.reset();
  }
}

void FakeControllerBase::CloseACLDataChannel() {
  if (acl_channel_.is_valid()) {
    acl_channel_wait_.Cancel();
    acl_channel_wait_.set_object(ZX_HANDLE_INVALID);
    acl_channel_.reset();
  }
}

void FakeControllerBase::HandleCommandPacket(
    async_dispatcher_t* dispatcher,
    async::WaitBase* wait,
    zx_status_t wait_status,
    const zx_packet_signal_t* signal) {
  common::StaticByteBuffer<hci::kMaxCommandPacketPayloadSize> buffer;
  uint32_t read_size;
  zx_status_t status = cmd_channel_.read(0u, buffer.mutable_data(),
                                         hci::kMaxCommandPacketPayloadSize,
                                         &read_size, nullptr, 0, nullptr);
  FXL_DCHECK(status == ZX_OK || status == ZX_ERR_PEER_CLOSED);
  if (status < 0) {
    if (status == ZX_ERR_PEER_CLOSED)
      FXL_LOG(INFO) << "Command channel was closed";
    else
      FXL_LOG(ERROR) << "Failed to read on cmd channel: "
                     << zx_status_get_string(status);

    CloseCommandChannel();
    return;
  }

  if (read_size < sizeof(hci::CommandHeader)) {
    FXL_LOG(ERROR) << "Malformed command packet received";
  } else {
    common::MutableBufferView view(buffer.mutable_data(), read_size);
    common::PacketView<hci::CommandHeader> packet(
        &view, read_size - sizeof(hci::CommandHeader));
    OnCommandPacketReceived(packet);
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on cmd channel: "
                   << zx_status_get_string(status);
    CloseCommandChannel();
  }
}

void FakeControllerBase::HandleACLPacket(
    async_dispatcher_t* dispatcher,
    async::WaitBase* wait,
    zx_status_t wait_status,
    const zx_packet_signal_t* signal) {
  common::StaticByteBuffer<hci::kMaxACLPayloadSize + sizeof(hci::ACLDataHeader)>
      buffer;
  uint32_t read_size;
  zx_status_t status =
      acl_channel_.read(0u, buffer.mutable_data(), buffer.size(), &read_size,
                        nullptr, 0, nullptr);
  FXL_DCHECK(status == ZX_OK || status == ZX_ERR_PEER_CLOSED);
  if (status < 0) {
    if (status == ZX_ERR_PEER_CLOSED)
      FXL_LOG(INFO) << "ACL channel was closed";
    else
      FXL_LOG(ERROR) << "Failed to read on ACL channel: "
                     << zx_status_get_string(status);

    CloseACLDataChannel();
    return;
  }

  common::BufferView view(buffer.data(), read_size);
  OnACLDataPacketReceived(view);

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on ACL channel: "
                   << zx_status_get_string(status);
    CloseACLDataChannel();
  }
}

}  // namespace testing
}  // namespace btlib
