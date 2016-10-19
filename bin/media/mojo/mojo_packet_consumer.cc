// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/mojo/mojo_packet_consumer.h"

namespace mojo {
namespace media {

MojoPacketConsumer::MojoPacketConsumer() {}

MojoPacketConsumer::~MojoPacketConsumer() {}

void MojoPacketConsumer::Bind(
    InterfaceRequest<MediaPacketConsumer> packet_consumer_request) {
  MediaPacketConsumerBase::Bind(packet_consumer_request.Pass());
}

void MojoPacketConsumer::SetFlushRequestedCallback(
    const FlushRequestedCallback& callback) {
  flush_requested_callback_ = callback;
}

void MojoPacketConsumer::OnPacketSupplied(
    std::unique_ptr<SuppliedPacket> supplied_packet) {
  FTL_DCHECK(supplied_packet);
  FTL_DCHECK(supply_callback_);
  supply_callback_(PacketImpl::Create(std::move(supplied_packet)));
}

void MojoPacketConsumer::OnPacketReturning() {
  if (downstream_demand_ == Demand::kPositive) {
    SetDemand(supplied_packets_outstanding() + 1);
  } else {
    SetDemand(supplied_packets_outstanding());
  }
}

void MojoPacketConsumer::OnFlushRequested(const FlushCallback& callback) {
  if (flush_requested_callback_) {
    flush_requested_callback_(callback);
  } else {
    FTL_DLOG(WARNING) << "flush requested but no callback registered";
    callback.Run();
  }
}

bool MojoPacketConsumer::can_accept_allocator() const {
  return false;
}

void MojoPacketConsumer::set_allocator(PayloadAllocator* allocator) {
  FTL_DLOG(ERROR) << "set_allocator called on MojoPacketConsumer";
}

void MojoPacketConsumer::SetSupplyCallback(
    const SupplyCallback& supply_callback) {
  supply_callback_ = supply_callback;
}

void MojoPacketConsumer::SetDownstreamDemand(Demand demand) {
  downstream_demand_ = demand;
  if (demand == Demand::kPositive &&
      supplied_packets_outstanding() >=
          current_demand().min_packets_outstanding) {
    SetDemand(supplied_packets_outstanding() + 1);
  }
}

MojoPacketConsumer::PacketImpl::PacketImpl(
    std::unique_ptr<SuppliedPacket> supplied_packet)
    : Packet(supplied_packet->packet()->pts,
             TimelineRate(supplied_packet->packet()->pts_rate_ticks,
                          supplied_packet->packet()->pts_rate_seconds),
             supplied_packet->packet()->end_of_stream,
             supplied_packet->payload_size(),
             supplied_packet->payload()),
      supplied_packet_(std::move(supplied_packet)) {}

MojoPacketConsumer::PacketImpl::~PacketImpl() {}

void MojoPacketConsumer::PacketImpl::Release() {
  delete this;
}

}  // namespace media
}  // namespace mojo
