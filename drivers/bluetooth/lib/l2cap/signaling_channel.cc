// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "signaling_channel.h"

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"

#include "channel.h"

namespace bluetooth {
namespace l2cap {
namespace internal {

SignalingChannel::SignalingChannel(std::unique_ptr<Channel> chan,
                                   hci::Connection::Role role)
    : is_open_(true),
      chan_(std::move(chan)),
      role_(role),
      task_runner_(fsl::MessageLoop::GetCurrent()->task_runner()) {
  FXL_DCHECK(chan_);
  FXL_DCHECK(chan_->id() == kSignalingChannelId ||
             chan_->id() == kLESignalingChannelId);
  FXL_DCHECK(task_runner_);

  chan_->set_channel_closed_callback(
      std::bind(&SignalingChannel::OnChannelClosed, this));

  auto rx_cb = rx_cb_factory_.MakeTask(
      std::bind(&SignalingChannel::OnRxBFrame, this, std::placeholders::_1));
  chan_->SetRxHandler(rx_cb, task_runner_);
}

SignalingChannel::~SignalingChannel() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  chan_ = nullptr;
}

bool SignalingChannel::SendPacket(CommandCode code,
                                  uint8_t identifier,
                                  const common::ByteBuffer& data) {
  return Send(BuildPacket(code, identifier, data));
}

bool SignalingChannel::Send(std::unique_ptr<const common::ByteBuffer> packet) {
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
    CommandCode code,
    uint8_t identifier,
    const common::ByteBuffer& data) {
  FXL_DCHECK(data.size());
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
  size_t length = sizeof(reason) + data.size();
  FXL_DCHECK(length <= sizeof(CommandRejectPayload));

  CommandRejectPayload reject;
  reject.reason = htole16(reason);

  if (data.size()) {
    FXL_DCHECK(data.size() <= kCommandRejectMaxDataLength);
    common::MutableBufferView rej_data(reject.data, data.size());
    rej_data.Write(data);
  }

  return SendPacket(kCommandRejectCode, identifier,
                    common::BufferView(&reject, length));
}

void SignalingChannel::OnChannelClosed() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(is_open());

  is_open_ = false;
  rx_cb_factory_.CancelAll();

  chan_->set_channel_closed_callback({});
  chan_->SetRxHandler({}, nullptr);
}

void SignalingChannel::OnRxBFrame(const SDU& sdu) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  if (!is_open())
    return;

  if (chan_->id() == kSignalingChannelId)
    ProcessBrEdrSigSdu(sdu);
  else
    ProcessLeSigSdu(sdu);
}

void SignalingChannel::ProcessLeSigSdu(const SDU& sdu) {
  // "[O]nly one command per C-frame shall be sent over [the LE] Fixed Channel"
  // (v5.0, Vol 3, Part A, Section 4).
  if (sdu.length() < sizeof(CommandHeader)) {
    FXL_VLOG(1)
        << "l2cap: SignalingChannel: dropped malformed LE signaling packet";
    return;
  }

  SDU::Reader reader(&sdu);

  auto func = [this](const auto& data) {
    SignalingPacket packet(&data);

    uint16_t cmd_length = le16toh(packet.header().length);
    if (cmd_length != data.size() - sizeof(CommandHeader)) {
      FXL_VLOG(1)
          << "l2cap: SignalingChannel: packet length mismatch (expected: "
          << cmd_length << ", recv: " << (data.size() - sizeof(CommandHeader))
          << "); drop";
      SendCommandReject(packet.header().id, RejectReason::kNotUnderstood,
                        common::BufferView());
      return;
    }

    ProcessPacket(SignalingPacket(&data, cmd_length));
  };

  // Performing a single read for the entire length of |sdu| can never fail.
  FXL_CHECK(reader.ReadNext(sdu.length(), func));
}

void SignalingChannel::ProcessBrEdrSigSdu(const SDU& sdu) {
  // "Multiple commands may be sent in a single C-frame over Fixed Channel CID
  // 0x0001 (ACL-U) (v5.0, Vol 3, Part A, Section 4)"
  // TODO(armansito): Implement.
}

void SignalingChannel::ProcessPacket(const SignalingPacket& packet) {
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
}  // namespace bluetooth
