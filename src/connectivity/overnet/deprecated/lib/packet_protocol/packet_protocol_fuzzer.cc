// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/packet_protocol/packet_protocol_fuzzer.h"

namespace overnet {

bool PacketProtocolFuzzer::BeginSend(uint8_t sender_idx, Slice data) {
  return packet_protocol(sender_idx).Then([this, data](PacketProtocol* pp) {
    pp->Send([data](auto arg) { return data; },
             [this](const Status& status) {
               if (done_)
                 return;
               switch (status.code()) {
                 case StatusCode::OK:
                 case StatusCode::UNAVAILABLE:
                 case StatusCode::CANCELLED:
                   break;
                 default:
                   std::cerr << "Expected each send to be ok, cancelled, or "
                                "unavailable, got: "
                             << status << "\n";
                   abort();
               }
             });
    return true;
  });
}

bool PacketProtocolFuzzer::CompleteSend(uint8_t sender_idx, uint8_t status) {
  Optional<Sender::PendingSend> send =
      sender(sender_idx).Then([](Sender* sender) { return sender->CompleteSend(); });
  if (!send)
    return false;
  auto slice =
      send->data(LazySliceArgs{Border::None(), std::numeric_limits<uint32_t>::max(), false});
  if (status == 0 && slice.length() > 0) {
    (*packet_protocol(3 - sender_idx))
        ->Process(timer_.Now(), send->seq, slice, [](auto process_status) {});
  }
  return true;
}

Optional<PacketProtocolFuzzer::Sender*> PacketProtocolFuzzer::sender(uint8_t idx) {
  switch (idx) {
    case 1:
      return &sender1_;
    case 2:
      return &sender2_;
    default:
      return Nothing;
  }
}

Optional<PacketProtocol*> PacketProtocolFuzzer::packet_protocol(uint8_t idx) {
  switch (idx) {
    case 1:
      return pp1_.get();
    case 2:
      return pp2_.get();
    default:
      return Nothing;
  }
}

void PacketProtocolFuzzer::Sender::SendPacket(SeqNum seq, LazySlice data) {
  pending_sends_.emplace(next_send_id_++, PendingSend{seq, std::move(data)});
}

Optional<PacketProtocolFuzzer::Sender::PendingSend> PacketProtocolFuzzer::Sender::CompleteSend() {
  auto it = pending_sends_.begin();
  if (it == pending_sends_.end())
    return Nothing;
  auto ps = std::move(it->second);
  pending_sends_.erase(it);
  return std::move(ps);
}

}  // namespace overnet
