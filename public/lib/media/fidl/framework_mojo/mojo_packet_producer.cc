// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "services/media/framework_mojo/mojo_packet_producer.h"

namespace mojo {
namespace media {

MojoPacketProducer::MojoPacketProducer() : binding_(this) {
  task_runner_ = base::MessageLoop::current()->task_runner();
  DCHECK(task_runner_);
}

MojoPacketProducer::~MojoPacketProducer() {}

void MojoPacketProducer::Bind(InterfaceRequest<MediaPacketProducer> request) {
  binding_.Bind(request.Pass());
}

void MojoPacketProducer::FlushConnection(
    const FlushConnectionCallback& callback) {
  if (is_connected()) {
    FlushConsumer(callback);
  } else {
    callback.Run();
  }
}

PayloadAllocator* MojoPacketProducer::allocator() {
  return this;
}

void MojoPacketProducer::SetDemandCallback(
    const DemandCallback& demand_callback) {
  demand_callback_ = demand_callback;
}

Demand MojoPacketProducer::SupplyPacket(PacketPtr packet) {
  DCHECK(packet);

  bool end_of_stream = packet->end_of_stream();

  // If we're not connected, throw the packet away.
  if (!is_connected()) {
    return end_of_stream ? Demand::kNegative : CurrentDemand();
  }

  task_runner_->PostTask(FROM_HERE,
                         base::Bind(&MojoPacketProducer::SendPacket,
                                    base::Unretained(this), packet.release()));

  return end_of_stream ? Demand::kNegative : CurrentDemand(1);
}

void MojoPacketProducer::Connect(InterfaceHandle<MediaPacketConsumer> consumer,
                                 const ConnectCallback& callback) {
  DCHECK(consumer);
  MediaPacketProducerBase::Connect(
      MediaPacketConsumerPtr::Create(std::move(consumer)).Pass(), callback);
}

void MojoPacketProducer::Disconnect() {
  if (demand_callback_) {
    demand_callback_(Demand::kNegative);
  }

  MediaPacketProducerBase::Disconnect();
}

void* MojoPacketProducer::AllocatePayloadBuffer(size_t size) {
  return MediaPacketProducerBase::AllocatePayloadBuffer(size);
}

void MojoPacketProducer::ReleasePayloadBuffer(void* buffer) {
  MediaPacketProducerBase::ReleasePayloadBuffer(buffer);
}

void MojoPacketProducer::OnDemandUpdated(uint32_t min_packets_outstanding,
                                         int64_t min_pts) {
  DCHECK(demand_callback_);
  demand_callback_(CurrentDemand());
}

void MojoPacketProducer::SendPacket(Packet* packet_raw_ptr) {
  DCHECK(packet_raw_ptr);

  ProducePacket(packet_raw_ptr->payload(), packet_raw_ptr->size(),
                packet_raw_ptr->pts(), packet_raw_ptr->end_of_stream(),
                [this, packet_raw_ptr]() {
                  PacketPtr packet = PacketPtr(packet_raw_ptr);
                  DCHECK(demand_callback_);
                  demand_callback_(CurrentDemand());
                });
}

void MojoPacketProducer::Reset() {
  if (binding_.is_bound()) {
    binding_.Close();
  }

  MediaPacketProducerBase::Reset();
}

Demand MojoPacketProducer::CurrentDemand(
    uint32_t additional_packets_outstanding) {
  if (!is_connected()) {
    return Demand::kNeutral;
  }

  // ShouldProducePacket tells us whether we should produce a packet based on
  // demand the consumer has expressed using mojo packet transport demand
  // semantics (min_packets_outstanding, min_pts). We need to translate this
  // into the producer's demand using framework demand semantics
  // (positive/neutral/negative).
  //
  // If we should send a packet, the producer signals positive demand so that
  // upstream components will deliver the needed packet. If we shouldn't send a
  // packet, the producer signals negative demand to prevent new packets from
  // arriving at the producer.
  //
  // If we express neutral demand instead of negative demand, packets would flow
  // freely downstream even though they're not demanded by the consumer. In
  // multistream (e.g audio/video) scenarios, this would cause serious problems.
  // If the demux has to produce a bunch of undemanded video packets in order to
  // find a demanded audio packet, neutral demand here would cause those video
  // packets to flow downstream, get decoded and queue up at the video renderer.
  // This wastes memory, because the decoded frames are so large. We would
  // rather the demux keep the undemanded video packets until they're demanded
  // so we get only the decoded frames we need, hence negative demand here.
  return ShouldProducePacket(additional_packets_outstanding)
             ? Demand::kPositive
             : Demand::kNegative;
}

}  // namespace media
}  // namespace mojo
