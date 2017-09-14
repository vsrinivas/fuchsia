// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/fidl/fidl_packet_producer.h"

#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/media/fidl/logs/media_packet_producer_channel.fidl.h"

namespace media {

FidlPacketProducer::FidlPacketProducer() : binding_(this) {
  task_runner_ = fsl::MessageLoop::GetCurrent()->task_runner();
  FXL_DCHECK(task_runner_);
}

FidlPacketProducer::~FidlPacketProducer() {
  Reset();
}

void FidlPacketProducer::Bind(
    fidl::InterfaceRequest<MediaPacketProducer> request) {
  binding_.Bind(std::move(request));
}

void FidlPacketProducer::SetConnectionStateChangedCallback(
    const ConnectionStateChangedCallback& callback) {
  connectionStateChangedCallback_ = callback;
}

void FidlPacketProducer::FlushConnection(
    bool hold_frame,
    const FlushConnectionCallback& callback) {
  if (is_connected()) {
    FlushConsumer(hold_frame, callback);
  } else {
    callback();
  }
}

PayloadAllocator* FidlPacketProducer::allocator() {
  return this;
}

Demand FidlPacketProducer::SupplyPacket(PacketPtr packet) {
  FXL_DCHECK(packet);

  bool end_of_stream = packet->end_of_stream();

  // If we're not connected, throw the packet away.
  if (!is_connected()) {
    return end_of_stream ? Demand::kNegative : CurrentDemand();
  }

  // We sample demand before posting the task that will SendPacket. By passing
  // 1 to CurrentDemand, we're asking what demand would be assuming we've
  // already sent the packet. Doing this before we post the task prevents a
  // race between this thread and the task_runner_ (FIDL) thread. Also, we're
  // potentially reporting demand on two different threads (the calling thread
  // and the FIDL thread via SetDemand), so the stage has to deal with the
  // possible races (it does).
  Demand demand = end_of_stream ? Demand::kNegative : CurrentDemand(1);

  task_runner_->PostTask(fxl::MakeCopyable([
    weak_this = std::weak_ptr<FidlPacketProducer>(shared_from_this()),
    packet = std::move(packet)
  ]() mutable {
    auto shared_this = weak_this.lock();
    if (shared_this) {
      shared_this->SendPacket(std::move(packet));
    }
  }));

  return demand;
}

void FidlPacketProducer::Connect(
    fidl::InterfaceHandle<MediaPacketConsumer> consumer,
    const ConnectCallback& callback) {
  FXL_DCHECK(consumer);
  MediaPacketProducerBase::Connect(
      MediaPacketConsumerPtr::Create(std::move(consumer)), callback);

  if (connectionStateChangedCallback_) {
    connectionStateChangedCallback_();
  }
}

void FidlPacketProducer::Disconnect() {
  stage().SetDemand(Demand::kNegative);

  MediaPacketProducerBase::Disconnect();

  if (connectionStateChangedCallback_) {
    connectionStateChangedCallback_();
  }
}

void* FidlPacketProducer::AllocatePayloadBuffer(size_t size) {
  return MediaPacketProducerBase::AllocatePayloadBuffer(size);
}

void FidlPacketProducer::ReleasePayloadBuffer(void* buffer) {
  MediaPacketProducerBase::ReleasePayloadBuffer(buffer);
}

void FidlPacketProducer::OnDemandUpdated(uint32_t min_packets_outstanding,
                                         int64_t min_pts) {
  stage().SetDemand(CurrentDemand());
}

void FidlPacketProducer::OnFailure() {
  if (connectionStateChangedCallback_) {
    connectionStateChangedCallback_();
  }
}

void FidlPacketProducer::SendPacket(PacketPtr packet) {
  FXL_DCHECK(packet);

  ProducePacket(packet->payload(), packet->size(), packet->pts(),
                packet->pts_rate(), packet->keyframe(), packet->end_of_stream(),
                MediaType::From(packet->revised_stream_type()),
                fxl::MakeCopyable([ this, packet = std::move(packet) ]() {
                  stage().SetDemand(CurrentDemand());
                }));
}

void FidlPacketProducer::Reset() {
  if (binding_.is_bound()) {
    binding_.Close();
  }

  MediaPacketProducerBase::Reset();
}

Demand FidlPacketProducer::CurrentDemand(
    uint32_t additional_packets_outstanding) {
  if (!is_connected()) {
    return Demand::kNeutral;
  }

  // ShouldProducePacket tells us whether we should produce a packet based on
  // demand the consumer has expressed using fidl packet transport demand
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
