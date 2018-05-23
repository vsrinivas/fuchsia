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

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
