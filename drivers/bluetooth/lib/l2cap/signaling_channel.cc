// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "signaling_channel.h"

#include <lib/async/default.h>
#include <lib/fit/function.h>

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "lib/fxl/logging.h"

#include "channel.h"

namespace btlib {
namespace l2cap {
namespace internal {

SignalingChannel::SignalingChannel(fbl::RefPtr<Channel> chan,
                                   hci::Connection::Role role)
    : is_open_(true),
      chan_(std::move(chan)),
      role_(role),
      next_cmd_id_(0x01),
      weak_ptr_factory_(this) {
  FXL_DCHECK(chan_);
  FXL_DCHECK(chan_->id() == kSignalingChannelId ||
             chan_->id() == kLESignalingChannelId);

  // Note: No need to guard against out-of-thread access as these callbacks are
  // called on the L2CAP thread.
  auto self = weak_ptr_factory_.GetWeakPtr();
  chan_->Activate(
      [self](const SDU& sdu) {
        if (self)
          self->OnRxBFrame(sdu);
      },
      [self] {
        if (self)
          self->OnChannelClosed();
      },
      async_get_default_dispatcher());
}

SignalingChannel::~SignalingChannel() { FXL_DCHECK(IsCreationThreadCurrent()); }

SignalingChannel::ResponderImpl::ResponderImpl(SignalingChannel* sig,
                                               CommandCode code, CommandId id)
    : sig_(sig), code_(code), id_(id) {
  FXL_DCHECK(sig_);
}

void SignalingChannel::ResponderImpl::Send(
    const common::ByteBuffer& rsp_payload) {
  sig()->SendPacket(code_, id_, rsp_payload);
}

void SignalingChannel::ResponderImpl::RejectNotUnderstood() {
  sig()->SendCommandReject(id_, RejectReason::kNotUnderstood,
                           common::BufferView());
}

void SignalingChannel::ResponderImpl::RejectInvalidChannelId(
    ChannelId local_cid, ChannelId remote_cid) {
  uint16_t ids[2];
  ids[0] = htole16(local_cid);
  ids[1] = htole16(remote_cid);
  sig()->SendCommandReject(id_, RejectReason::kInvalidCID,
                           common::BufferView(ids, sizeof(ids)));
}

bool SignalingChannel::SendPacket(CommandCode code, uint8_t identifier,
                                  const common::ByteBuffer& data) {
  FXL_DCHECK(IsCreationThreadCurrent());
  return Send(BuildPacket(code, identifier, data));
}

bool SignalingChannel::Send(std::unique_ptr<const common::ByteBuffer> packet) {
  FXL_DCHECK(IsCreationThreadCurrent());
  FXL_DCHECK(packet);
  FXL_DCHECK(packet->size() >= sizeof(CommandHeader));

  if (!is_open())
    return false;

  // While 0x00 is an illegal command identifier (see v5.0, Vol 3, Part A,
  // Section 4) we don't assert that here. When we receive a command that uses
  // 0 as the identifier, we reject the command and use that identifier in the
  // response rather than assert and crash.
  __UNUSED SignalingPacket reply(packet.get(),
                                 packet->size() - sizeof(CommandHeader));
  FXL_DCHECK(reply.header().code);
  FXL_DCHECK(reply.payload_size() == le16toh(reply.header().length));
  FXL_DCHECK(chan_);

  return chan_->Send(std::move(packet));
}

std::unique_ptr<common::ByteBuffer> SignalingChannel::BuildPacket(
    CommandCode code, uint8_t identifier, const common::ByteBuffer& data) {
  FXL_DCHECK(data.size() <= std::numeric_limits<uint16_t>::max());

  auto buffer = common::NewSlabBuffer(sizeof(CommandHeader) + data.size());
  FXL_CHECK(buffer);

  MutableSignalingPacket packet(buffer.get(), data.size());
  packet.mutable_header()->code = code;
  packet.mutable_header()->id = identifier;
  packet.mutable_header()->length = htole16(static_cast<uint16_t>(data.size()));
  packet.mutable_payload_data().Write(data);

  return buffer;
}

bool SignalingChannel::SendCommandReject(uint8_t identifier,
                                         RejectReason reason,
                                         const common::ByteBuffer& data) {
  FXL_DCHECK(data.size() <= kCommandRejectMaxDataLength);

  constexpr size_t kMaxPayloadLength =
      sizeof(CommandRejectPayload) + kCommandRejectMaxDataLength;
  common::StaticByteBuffer<kMaxPayloadLength> rej_buf;

  common::MutablePacketView<CommandRejectPayload> reject(&rej_buf, data.size());
  reject.mutable_header()->reason = htole16(reason);
  reject.mutable_payload_data().Write(data);

  return SendPacket(kCommandRejectCode, identifier, reject.data());
}

CommandId SignalingChannel::GetNextCommandId() {
  // Recycling identifiers is permitted and only 0x00 is invalid (v5.0 Vol 3,
  // Part A, Section 4).
  const auto cmd = next_cmd_id_++;
  if (next_cmd_id_ == kInvalidCommandId) {
    next_cmd_id_ = 0x01;
  }

  return cmd;
}

void SignalingChannel::OnChannelClosed() {
  FXL_DCHECK(IsCreationThreadCurrent());
  FXL_DCHECK(is_open());

  is_open_ = false;
}

void SignalingChannel::OnRxBFrame(const SDU& sdu) {
  FXL_DCHECK(IsCreationThreadCurrent());

  if (!is_open())
    return;

  DecodeRxUnit(
      sdu, fit::bind_member(this, &SignalingChannel::CheckAndDispatchPacket));
}

void SignalingChannel::CheckAndDispatchPacket(const SignalingPacket& packet) {
  if (packet.size() > mtu()) {
    // Respond with our signaling MTU.
    uint16_t rsp_mtu = htole16(mtu());
    common::BufferView rej_data(&rsp_mtu, sizeof(rsp_mtu));
    SendCommandReject(packet.header().id, RejectReason::kSignalingMTUExceeded,
                      rej_data);
  } else if (!packet.header().id) {
    // "Signaling identifier 0x00 is an illegal identifier and shall never be
    // used in any command" (v5.0, Vol 3, Part A, Section 4).
    FXL_VLOG(1) << "l2cap: SignalingChannel: illegal sig. ID: 0x00; drop";
    SendCommandReject(packet.header().id, RejectReason::kNotUnderstood,
                      common::BufferView());
  } else if (!HandlePacket(packet)) {
    SendCommandReject(packet.header().id, RejectReason::kNotUnderstood,
                      common::BufferView());
  }
}

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
