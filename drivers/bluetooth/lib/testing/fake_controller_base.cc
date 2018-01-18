// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_controller_base.h"

#include <zircon/status.h>

#include "garnet/drivers/bluetooth/lib/hci/acl_data_packet.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"

namespace btlib {
namespace testing {

FakeControllerBase::FakeControllerBase(zx::channel cmd_channel,
                                       zx::channel acl_data_channel)
    : cmd_channel_(std::move(cmd_channel)),
      acl_channel_(std::move(acl_data_channel)) {}

FakeControllerBase::~FakeControllerBase() {
  // When this destructor gets called any subclass state will be undefined. If
  // Stop() has not been called before reaching this point this can cause
  // runtime errors when our MessageLoop handlers attempt to invoke the pure
  // virtual methods of this class. So we require that the FakeController be
  // stopped by now.
  FXL_DCHECK(!IsStarted());
}

void FakeControllerBase::Start() {
  FXL_DCHECK(!IsStarted());
  FXL_DCHECK(cmd_channel_.is_valid());

  cmd_channel_wait_.set_object(cmd_channel_.get());
  cmd_channel_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  cmd_channel_wait_.set_handler(
      fbl::BindMember(this, &FakeControllerBase::HandleCommandPacket));

  zx_status_t status = cmd_channel_wait_.Begin(async_get_default());
  FXL_DCHECK(status == ZX_OK);

  if (acl_channel_.is_valid()) {
    acl_channel_wait_.set_object(acl_channel_.get());
    acl_channel_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    acl_channel_wait_.set_handler(
        fbl::BindMember(this, &FakeControllerBase::HandleACLPacket));
    zx_status_t status = acl_channel_wait_.Begin(async_get_default());
    FXL_DCHECK(status == ZX_OK);
  }
}

void FakeControllerBase::Stop() {
  FXL_DCHECK(IsStarted());

  CloseCommandChannel();
  CloseACLDataChannel();
}

void FakeControllerBase::SendCommandChannelPacket(
    const common::ByteBuffer& packet) {
  FXL_DCHECK(IsStarted());
  zx_status_t status =
      cmd_channel_.write(0, packet.data(), packet.size(), nullptr, 0);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "FakeController: Failed to write to control channel: "
                     << zx_status_get_string(status);
  }
}

void FakeControllerBase::SendACLDataChannelPacket(
    const common::ByteBuffer& packet) {
  FXL_DCHECK(IsStarted());
  zx_status_t status =
      acl_channel_.write(0, packet.data(), packet.size(), nullptr, 0);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "FakeController: Failed to write to ACL data channel: "
                     << zx_status_get_string(status);
  }
}

void FakeControllerBase::CloseCommandChannel() {
  cmd_channel_wait_.Cancel(async_get_default());
  cmd_channel_wait_.set_object(ZX_HANDLE_INVALID);
  cmd_channel_.reset();
}

void FakeControllerBase::CloseACLDataChannel() {
  acl_channel_wait_.Cancel(async_get_default());
  acl_channel_wait_.set_object(ZX_HANDLE_INVALID);
  acl_channel_.reset();
}

async_wait_result_t FakeControllerBase::HandleCommandPacket(
    async_t* async,
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
    return ASYNC_WAIT_FINISHED;
  }

  if (read_size < sizeof(hci::CommandHeader)) {
    FXL_LOG(ERROR) << "Malformed command packet received";
    return ASYNC_WAIT_AGAIN;
  }

  common::MutableBufferView view(buffer.mutable_data(), read_size);
  common::PacketView<hci::CommandHeader> packet(
      &view, read_size - sizeof(hci::CommandHeader));
  OnCommandPacketReceived(packet);
  return ASYNC_WAIT_AGAIN;
}

async_wait_result_t FakeControllerBase::HandleACLPacket(
    async_t* async,
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
    return ASYNC_WAIT_FINISHED;
  }

  common::BufferView view(buffer.data(), read_size);
  OnACLDataPacketReceived(view);
  return ASYNC_WAIT_AGAIN;
}

}  // namespace testing
}  // namespace btlib
