// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/links/packet_link.h"

#include <iostream>
#include <sstream>

namespace overnet {

PacketLink::PacketLink(Router* router, NodeId peer, uint32_t mss,
                       uint64_t label)
    : router_(router),
      timer_(router->timer()),
      peer_(peer),
      label_(label),
      protocol_{router_->timer(),
                [router] { return (*router->rng())(); },
                this,
                PacketProtocol::PlaintextCodec(),
                mss,
                false},
      packet_stuffer_(router->node_id(), peer) {}

void PacketLink::Close(Callback<void> quiesced) {
  ScopedModule<PacketLink> scoped_module(this);
  closed_ = true;
  packet_stuffer_.DropPendingMessages();
  protocol_.Close(std::move(quiesced));
}

void PacketLink::Forward(Message message) {
  ScopedModule<PacketLink> scoped_module(this);
  const bool send_immediately =
      packet_stuffer_.Forward(std::move(message)) && !sending_;
  OVERNET_TRACE(DEBUG) << "Forward sending=" << sending_
                       << " imm=" << send_immediately;
  if (send_immediately) {
    SchedulePacket();
  }
}

void PacketLink::Tombstone() {
  metrics_version_ = fuchsia::overnet::protocol::METRIC_VERSION_TOMBSTONE;
}

fuchsia::overnet::protocol::LinkStatus PacketLink::GetLinkStatus() {
  ScopedModule<PacketLink> scoped_module(this);

  if (metrics_version_ ==
      fuchsia::overnet::protocol::METRIC_VERSION_TOMBSTONE) {
    return fuchsia::overnet::protocol::LinkStatus{router_->node_id().as_fidl(),
                                                  peer_.as_fidl(), label_,
                                                  metrics_version_};
  }

  // Advertise MSS as smaller than it is to account for some bugs that exist
  // right now.
  // TODO(ctiller): eliminate this - we should be precise.
  constexpr uint32_t kUnderadvertiseMaximumSendSize = 32;
  fuchsia::overnet::protocol::LinkStatus m{router_->node_id().as_fidl(),
                                           peer_.as_fidl(), label_,
                                           metrics_version_++};
  m.metrics.set_bw_link(protocol_.bottleneck_bandwidth().bits_per_second());
  m.metrics.set_rtt(protocol_.round_trip_time().as_us());
  m.metrics.set_mss(
      std::max(kUnderadvertiseMaximumSendSize, protocol_.maximum_send_size()) -
      kUnderadvertiseMaximumSendSize);
  return m;
}

void PacketLink::SchedulePacket() {
  assert(!sending_);
  assert(packet_stuffer_.HasPendingMessages());
  auto r = new LinkSendRequest(this);
  OVERNET_TRACE(DEBUG) << "Schedule " << r;
  protocol_.Send(PacketProtocol::SendRequestHdl(r));
}

PacketLink::LinkSendRequest::LinkSendRequest(PacketLink* link) : link_(link) {
  OVERNET_TRACE(DEBUG) << "LinkSendRequest[" << this << "]: Create";
  assert(!link->sending_);
  link->sending_ = true;
}

PacketLink::LinkSendRequest::~LinkSendRequest() {
  assert(!blocking_sends_);
  OVERNET_TRACE(DEBUG) << "LinkSendRequest[" << this << "]: Destroy";
}

Slice PacketLink::LinkSendRequest::GenerateBytes(LazySliceArgs args) {
  auto link = link_;
  ScopedModule<PacketLink> scoped_module(link_);
  ScopedOp scoped_op(op_);
  OVERNET_TRACE(DEBUG) << "LinkSendRequest[" << this << "]: GenerateBytes";
  assert(blocking_sends_);
  assert(link->sending_);
  blocking_sends_ = false;
  auto pkt = link->packet_stuffer_.BuildPacket(args);
  link->sending_ = false;
  OVERNET_TRACE(DEBUG) << "LinkSendRequest[" << this << "]: Generated " << pkt;
  if (link->packet_stuffer_.HasPendingMessages()) {
    link->SchedulePacket();
  }
  return pkt;
}

void PacketLink::LinkSendRequest::Ack(const Status& status) {
  ScopedModule<PacketLink> scoped_module(link_);
  ScopedOp scoped_op(op_);
  OVERNET_TRACE(DEBUG) << "LinkSendRequest[" << this
                       << "]: Ack status=" << status
                       << " blocking_sends=" << blocking_sends_;
  if (blocking_sends_) {
    assert(status.is_error());
    assert(link_->sending_);
    link_->sending_ = false;
    blocking_sends_ = false;
    if (link_->packet_stuffer_.HasPendingMessages()) {
      link_->SchedulePacket();
    }
  }
  delete this;
}

void PacketLink::SendPacket(SeqNum seq, LazySlice data) {
  if (send_packet_queue_ != nullptr) {
    send_packet_queue_->emplace(std::move(data));
    return;
  }

  PacketProtocol::ProtocolRef protocol_ref(&protocol_);
  std::queue<LazySlice> send_packet_queue;
  send_packet_queue_ = &send_packet_queue;

  for (;;) {
    const auto prefix_length = 1 + seq.wire_length();
    auto data_slice = data(
        LazySliceArgs{Border::Prefix(prefix_length),
                      protocol_.maximum_send_size() - prefix_length, false});
    auto send_slice = data_slice.WithPrefix(prefix_length, [seq](uint8_t* p) {
      *p++ = 0;
      seq.Write(p);
    });
    OVERNET_TRACE(DEBUG) << "Emit " << send_slice;
    Emit(std::move(send_slice));

    if (send_packet_queue.empty()) {
      break;
    }

    data = std::move(send_packet_queue.front());
    send_packet_queue.pop();
  }

  send_packet_queue_ = nullptr;
}

void PacketLink::NoConnectivity() {
  OVERNET_TRACE(WARNING) << "No route to link destination";
  Tombstone();
}

void PacketLink::Process(TimeStamp received, Slice packet) {
  if (closed_) {
    return;
  }

  ScopedModule<PacketLink> scoped_module(this);
  const uint8_t* const begin = packet.begin();
  const uint8_t* p = begin;
  const uint8_t* const end = packet.end();

  if (p == end) {
    OVERNET_TRACE(WARNING) << "Empty packet";
    return;
  }
  if (*p != 0) {
    OVERNET_TRACE(WARNING) << "Non-zero op-code received in PacketLink";
    return;
  }
  ++p;

  // Packets without sequence numbers are used to end the three way handshake.
  if (p == end) {
    return;
  }

  auto seq_status = SeqNum::Parse(&p, end);
  if (seq_status.is_error()) {
    OVERNET_TRACE(WARNING) << "Packet seqnum parse failure: "
                           << seq_status.AsStatus();
    return;
  }
  packet.TrimBegin(p - begin);
  // begin, p, end are no longer valid.
  protocol_.Process(
      received, *seq_status.get(), std::move(packet),
      [this, received](auto packet_status) {
        if (packet_status.is_error()) {
          if (packet_status.code() != StatusCode::CANCELLED) {
            OVERNET_TRACE(WARNING)
                << "Packet header parse failure: " << packet_status.AsStatus();
          }
          return;
        }
        if (auto* msg = *packet_status) {
          auto body_status = ProcessBody(received, std::move(msg->payload));
          if (body_status.is_error()) {
            OVERNET_TRACE(WARNING)
                << "Packet body parse failure: " << body_status;
            return;
          }
        }
      });
}

Status PacketLink::ProcessBody(TimeStamp received, Slice packet) {
  return packet_stuffer_.ParseAndForwardTo(received, std::move(packet),
                                           router_);
}

}  // namespace overnet
