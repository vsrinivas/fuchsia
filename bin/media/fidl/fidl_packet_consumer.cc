// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/fidl/fidl_packet_consumer.h"

namespace media {

FidlPacketConsumer::FidlPacketConsumer() {}

FidlPacketConsumer::~FidlPacketConsumer() {}

void FidlPacketConsumer::Bind(
    fidl::InterfaceRequest<MediaPacketConsumer> packet_consumer_request,
    const std::function<void()>& unbind_handler) {
  unbind_handler_ = unbind_handler;
  MediaPacketConsumerBase::Bind(std::move(packet_consumer_request));
}

void FidlPacketConsumer::SetFlushRequestedCallback(
    const FlushRequestedCallback& callback) {
  flush_requested_callback_ = callback;
}

void FidlPacketConsumer::OnPacketSupplied(
    std::unique_ptr<SuppliedPacket> supplied_packet) {
  FTL_DCHECK(supplied_packet);
  FTL_DCHECK(supply_callback_);
  supply_callback_(PacketImpl::Create(std::move(supplied_packet)));
}

void FidlPacketConsumer::OnPacketReturning() {
  uint32_t demand = supplied_packets_outstanding();

  if (downstream_demand_ == Demand::kPositive || demand == 0) {
    ++demand;
  }

  SetDemand(demand);
}

void FidlPacketConsumer::OnFlushRequested(bool hold_frame,
                                          const FlushCallback& callback) {
  if (flush_requested_callback_) {
    flush_requested_callback_(hold_frame, callback);
  } else {
    FTL_DLOG(WARNING) << "flush requested but no callback registered";
    callback();
  }
}

void FidlPacketConsumer::OnUnbind() {
  if (unbind_handler_) {
    std::function<void()> unbind_handler(std::move(unbind_handler_));
    unbind_handler();
  }
}

bool FidlPacketConsumer::can_accept_allocator() const {
  return false;
}

void FidlPacketConsumer::set_allocator(PayloadAllocator* allocator) {
  FTL_DLOG(ERROR) << "set_allocator called on FidlPacketConsumer";
}

void FidlPacketConsumer::SetSupplyCallback(
    const SupplyCallback& supply_callback) {
  supply_callback_ = supply_callback;
}

void FidlPacketConsumer::SetDownstreamDemand(Demand demand) {
  downstream_demand_ = demand;
  if (demand == Demand::kPositive &&
      supplied_packets_outstanding() >=
          current_demand().min_packets_outstanding) {
    SetDemand(supplied_packets_outstanding() + 1);
  }
}

FidlPacketConsumer::PacketImpl::PacketImpl(
    std::unique_ptr<SuppliedPacket> supplied_packet)
    : Packet(supplied_packet->packet()->pts,
             TimelineRate(supplied_packet->packet()->pts_rate_ticks,
                          supplied_packet->packet()->pts_rate_seconds),
             supplied_packet->packet()->keyframe,
             supplied_packet->packet()->end_of_stream,
             supplied_packet->payload_size(),
             supplied_packet->payload()),
      supplied_packet_(std::move(supplied_packet)) {}

FidlPacketConsumer::PacketImpl::~PacketImpl() {}

void FidlPacketConsumer::PacketImpl::Release() {
  delete this;
}

}  // namespace media
