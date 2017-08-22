// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/fidl/fidl_packet_consumer.h"

#include "apps/media/src/fidl/fidl_type_conversions.h"
#include "lib/mtl/tasks/message_loop.h"

namespace media {

FidlPacketConsumer::FidlPacketConsumer() {}

FidlPacketConsumer::~FidlPacketConsumer() {}

void FidlPacketConsumer::Bind(
    fidl::InterfaceRequest<MediaPacketConsumer> packet_consumer_request,
    const std::function<void()>& unbind_handler) {
  unbind_handler_ = unbind_handler;
  task_runner_ = mtl::MessageLoop::GetCurrent()->task_runner();
  FTL_DCHECK(task_runner_);
  MediaPacketConsumerBase::Bind(std::move(packet_consumer_request));
}

void FidlPacketConsumer::SetFlushRequestedCallback(
    const FlushRequestedCallback& callback) {
  flush_requested_callback_ = callback;
}

void FidlPacketConsumer::OnPacketSupplied(
    std::unique_ptr<SuppliedPacket> supplied_packet) {
  FTL_DCHECK(supplied_packet);
  stage().SupplyPacket(PacketImpl::Create(std::move(supplied_packet)));
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

void FidlPacketConsumer::SetDownstreamDemand(Demand demand) {
  downstream_demand_ = demand;
  if (demand == Demand::kPositive &&
      supplied_packets_outstanding() >=
          current_demand().min_packets_outstanding) {
    task_runner_->PostTask([
      this, demand = supplied_packets_outstanding() + 1
    ]() { SetDemand(demand); });
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
      supplied_packet_(std::move(supplied_packet)) {
  if (supplied_packet_->packet()->revised_media_type) {
    SetRevisedStreamType(
        supplied_packet_->packet()
            ->revised_media_type.To<std::unique_ptr<StreamType>>());
  }
}

uint64_t FidlPacketConsumer::PacketImpl::GetLabel() {
  return supplied_packet_->label();
}

}  // namespace media
