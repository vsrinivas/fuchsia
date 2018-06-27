// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/l2cap/bredr_signaling_channel.h"

#include "lib/fxl/strings/string_printf.h"

#include <zircon/compiler.h>

namespace btlib {
namespace l2cap {
namespace internal {

BrEdrSignalingChannel::BrEdrSignalingChannel(fbl::RefPtr<Channel> chan,
                                             hci::Connection::Role role)
    : SignalingChannel(std::move(chan), role) {
  set_mtu(kDefaultMTU);
}

// This is implemented as v5.0 Vol 3, Part A Section 4.8: "These requests may be
// used for testing the link or for passing vendor specific information using
// the optional data field."
bool BrEdrSignalingChannel::TestLink(const common::ByteBuffer& data,
                                     BrEdrSignalingChannel::DataCallback cb) {
  const CommandId id = EnqueueResponse(
      kEchoResponse, [cb = std::move(cb)](const SignalingPacket& packet) {
        if (packet.header().code == kCommandRejectCode) {
          cb(common::BufferView());
        } else {
          cb(packet.payload_data());
        }
      });

  if (id == kInvalidCommandId) {
    return false;
  }

  return SendPacket(kEchoRequest, id, data);
}

void BrEdrSignalingChannel::DecodeRxUnit(const SDU& sdu,
                                         const PacketDispatchCallback& cb) {
  // "Multiple commands may be sent in a single C-frame over Fixed Channel CID
  // 0x0001 (ACL-U) (v5.0, Vol 3, Part A, Section 4)"
  if (sdu.length() < sizeof(CommandHeader)) {
    FXL_VLOG(1)
        << "l2cap: SignalingChannel: dropped malformed ACL signaling packet";
    return;
  }

  SDU::Reader reader(&sdu);

  auto split_and_process_packets_from_sdu =
      [&cb, this](const common::ByteBuffer& sdu_data) {
        size_t sdu_offset = 0;

        while (sdu_offset + sizeof(CommandHeader) <= sdu_data.size()) {
          auto& header_data = sdu_data.view(sdu_offset, sizeof(CommandHeader));
          SignalingPacket packet(&header_data);

          uint16_t expected_payload_length = le16toh(packet.header().length);
          size_t remaining_sdu_length =
              sdu_data.size() - sdu_offset - sizeof(CommandHeader);
          if (remaining_sdu_length < expected_payload_length) {
            FXL_VLOG(1) << fxl::StringPrintf(
                "l2cap: SignalingChannel: expected more bytes in packet (%zu < "
                "%u); drop",
                remaining_sdu_length, expected_payload_length);
            SendCommandReject(packet.header().id, RejectReason::kNotUnderstood,
                              common::BufferView());
            return;
          }

          auto& packet_data = sdu_data.view(
              sdu_offset, sizeof(CommandHeader) + expected_payload_length);
          cb(SignalingPacket(&packet_data, expected_payload_length));

          sdu_offset += packet_data.size();
        }

        if (sdu_offset != sdu_data.size()) {
          FXL_VLOG(1) << "l2cap: SignalingChannel: incomplete packet header "
                         "(expected: "
                      << sizeof(CommandHeader)
                      << ", left: " << sdu_data.size() - sdu_offset << ")";
        }
      };

  // Performing a single read for the entire length of an SDU can never fail.
  FXL_CHECK(reader.ReadNext(sdu.length(), split_and_process_packets_from_sdu));
}

bool BrEdrSignalingChannel::HandlePacket(const SignalingPacket& packet) {
  if (IsSupportedResponse(packet.header().code)) {
    OnRxResponse(packet);
    return true;
  }

  // Handle request commands from remote.
  switch (packet.header().code) {
    case kEchoRequest:
      SendPacket(kEchoResponse, packet.header().id, packet.payload_data());
      return true;

    default:
      FXL_VLOG(1) << fxl::StringPrintf(
          "l2cap: BR/EDR sig: Unsupported code %#04x", packet.header().code);
      break;
  }

  return false;
}

CommandId BrEdrSignalingChannel::EnqueueResponse(
    CommandCode expected_code, BrEdrSignalingChannel::ResponseHandler handler) {
  FXL_DCHECK(IsSupportedResponse(expected_code));

  // Command identifiers for pending requests are assumed to be unique across
  // all types of requests and reused by order of least recent use. See v5.0
  // Vol 3, Part A Section 4.
  //
  // Uniqueness across different command types: "Within each signaling channel a
  // different Identifier shall be used for each successive command"
  // Reuse order: "the Identifier may be recycled if all other Identifiers have
  // subsequently been used"
  const CommandId initial_id = GetNextCommandId();
  CommandId id;
  for (id = initial_id; IsCommandPending(id);) {
    id = GetNextCommandId();

    if (id == initial_id) {
      FXL_LOG(ERROR) << fxl::StringPrintf(
          "l2cap: BR/EDR sig: all valid command IDs in use for pending "
          " requests; can't queue expected response command %#04x",
          expected_code);
      return kInvalidCommandId;
    }
  }

  pending_commands_[id] = std::make_pair(expected_code, std::move(handler));
  return id;
}

bool BrEdrSignalingChannel::IsSupportedResponse(CommandCode code) const {
  switch (code) {
    case kCommandRejectCode:
    case kConnectionResponse:
    case kConfigurationResponse:
    case kDisconnectResponse:
    case kEchoResponse:
    case kInformationResponse:
      return true;
  }

  // Other response-type commands are for AMP and are not supported.
  return false;
}

bool BrEdrSignalingChannel::IsCommandPending(CommandId id) const {
  return pending_commands_.find(id) != pending_commands_.end();
}

void BrEdrSignalingChannel::OnRxResponse(const SignalingPacket& packet) {
  auto iter = pending_commands_.find(packet.header().id);
  if (iter == pending_commands_.end()) {
    FXL_VLOG(2) << fxl::StringPrintf(
        "l2cap: BR/EDR sig: Ignoring unexpected response, id %#04x",
        packet.header().id);

    SendCommandReject(packet.header().id, RejectReason::kNotUnderstood,
                      common::BufferView());
    return;
  }

  if (packet.header().code != iter->second.first &&
      packet.header().code != kCommandRejectCode) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "l2cap: BR/EDR sig: Response (id %#04x) has unexpected code %#04x",
        packet.header().id, packet.header().code);

    SendCommandReject(packet.header().id, RejectReason::kNotUnderstood,
                      common::BufferView());
    return;
  }

  auto handler = std::move(iter->second.second);
  pending_commands_.erase(iter);
  handler(packet);
}

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
