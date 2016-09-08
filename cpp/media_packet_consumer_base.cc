// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/cpp/media_packet_consumer_base.h"

#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

#if defined(FLOG_ENABLED)

namespace {

// Gets the size of a shared buffer.
uint64_t SizeOf(const ScopedSharedBufferHandle& handle) {
  MojoBufferInformation info;
  MojoResult result =
      MojoGetBufferInformation(handle.get().value(), &info, sizeof(info));
  return result == MOJO_RESULT_OK ? info.num_bytes : 0;
}

}  // namespace

#endif  // !defined(NDEBUG)

// For checking preconditions when handling mojo requests.
// Checks the condition, and, if it's false, calls Fail and returns.
#define RCHECK(condition, message) \
  if (!(condition)) {              \
    FTL_DLOG(ERROR) << message;    \
    Fail();                        \
    return;                        \
  }

MediaPacketConsumerBase::MediaPacketConsumerBase() : binding_(this) {
  Reset();
  FTL_DCHECK(counter_);
}

MediaPacketConsumerBase::~MediaPacketConsumerBase() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  // Prevent the counter from calling us back.
  counter_->Detach();

  if (binding_.is_bound()) {
    binding_.Close();
  }
}

void MediaPacketConsumerBase::Bind(
    InterfaceRequest<MediaPacketConsumer> request) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  binding_.Bind(request.Pass());
  binding_.set_connection_error_handler([this]() { Reset(); });
}

bool MediaPacketConsumerBase::is_bound() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  return binding_.is_bound();
}

void MediaPacketConsumerBase::SetDemand(uint32_t min_packets_outstanding,
                                        int64_t min_pts) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (min_packets_outstanding == demand_.min_packets_outstanding &&
      min_pts == demand_.min_pts) {
    // Demand hasn't changed. Nothing to do.
    return;
  }

  demand_.min_packets_outstanding = min_packets_outstanding;
  demand_.min_pts = min_pts;

  FLOG(log_channel_, DemandSet(demand_.Clone()));
  demand_update_required_ = true;

  MaybeCompletePullDemandUpdate();
}

void MediaPacketConsumerBase::Reset() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FLOG(log_channel_, Reset());
  if (binding_.is_bound()) {
    binding_.Close();
  }

  demand_.min_packets_outstanding = 0;
  demand_.min_pts = MediaPacket::kNoTimestamp;

  get_demand_update_callback_.reset();

  if (counter_) {
    counter_->Detach();
  }
  counter_ = std::make_shared<SuppliedPacketCounter>(this);
}

void MediaPacketConsumerBase::Fail() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FLOG(log_channel_, Failed());
  Reset();
  OnFailure();
}

void MediaPacketConsumerBase::OnPacketReturning() {}

void MediaPacketConsumerBase::OnFlushRequested(const FlushCallback& callback) {
  callback.Run();
}

void MediaPacketConsumerBase::OnFailure() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
}

void MediaPacketConsumerBase::PullDemandUpdate(
    const PullDemandUpdateCallback& callback) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (!get_demand_update_callback_.is_null()) {
    // There's already a pending request. This isn't harmful, but it indicates
    // that the client doesn't know what it's doing.
    FTL_DLOG(WARNING) << "PullDemandUpdate was called when another "
                         "PullDemandUpdate call was pending";
    FLOG(log_channel_, RespondingToGetDemandUpdate(demand_.Clone()));
    get_demand_update_callback_.Run(demand_.Clone());
  }

  get_demand_update_callback_ = callback;

  MaybeCompletePullDemandUpdate();
}

void MediaPacketConsumerBase::AddPayloadBuffer(
    uint32_t payload_buffer_id,
    ScopedSharedBufferHandle payload_buffer) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FTL_DCHECK(payload_buffer.is_valid());
  FLOG(log_channel_,
       AddPayloadBufferRequested(payload_buffer_id, SizeOf(payload_buffer)));
  MojoResult result = counter_->buffer_set().AddBuffer(payload_buffer_id,
                                                       payload_buffer.Pass());
  RCHECK(result == MOJO_RESULT_OK, "failed to map buffer");
}

void MediaPacketConsumerBase::RemovePayloadBuffer(uint32_t payload_buffer_id) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FLOG(log_channel_, RemovePayloadBufferRequested(payload_buffer_id));
  counter_->buffer_set().RemoveBuffer(payload_buffer_id);
}

void MediaPacketConsumerBase::SupplyPacket(
    MediaPacketPtr media_packet,
    const SupplyPacketCallback& callback) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FTL_DCHECK(media_packet);

  void* payload;
  if (media_packet->payload_size == 0) {
    payload = nullptr;
  } else {
    RCHECK(counter_->buffer_set().Validate(
               SharedBufferSet::Locator(media_packet->payload_buffer_id,
                                        media_packet->payload_offset),
               media_packet->payload_size),
           "invalid buffer region");
    payload = counter_->buffer_set().PtrFromLocator(SharedBufferSet::Locator(
        media_packet->payload_buffer_id, media_packet->payload_offset));
  }

  uint64_t label = ++prev_packet_label_;
  FLOG(log_channel_,
       PacketSupplied(label, media_packet.Clone(), FLOG_ADDRESS(payload),
                      counter_->packets_outstanding() + 1));
  OnPacketSupplied(std::unique_ptr<SuppliedPacket>(new SuppliedPacket(
      label, media_packet.Pass(), payload, callback, counter_)));
}

void MediaPacketConsumerBase::Flush(const FlushCallback& callback) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FLOG(log_channel_, FlushRequested());

  demand_.min_packets_outstanding = 0;
  demand_.min_pts = MediaPacket::kNoTimestamp;

  OnFlushRequested([this, callback]() {
    FLOG(log_channel_, CompletingFlush());
    callback.Run();
  });
}

void MediaPacketConsumerBase::MaybeCompletePullDemandUpdate() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  // If we're in the middle of returning a packet, we want to use the
  // SupplyPacket callback for demand updates rather than the PullDemandUpdate
  // callback.
  if (!demand_update_required_ || returning_packet_ ||
      get_demand_update_callback_.is_null()) {
    return;
  }

  FLOG(log_channel_, RespondingToGetDemandUpdate(demand_.Clone()));
  demand_update_required_ = false;
  get_demand_update_callback_.Run(demand_.Clone());
  get_demand_update_callback_.reset();
}

MediaPacketDemandPtr MediaPacketConsumerBase::GetDemandForPacketDeparture(
    uint64_t label) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  FLOG(log_channel_, ReturningPacket(label, counter_->packets_outstanding()));

  // Note that we're returning a packet so that MaybeCompletePullDemandUpdate
  // won't try to send a packet update via a PullDemandUpdate callback.
  returning_packet_ = true;
  // This is the subclass's chance to SetDemand.
  OnPacketReturning();
  returning_packet_ = false;

  if (!demand_update_required_) {
    return nullptr;
  }

  demand_update_required_ = false;
  return demand_.Clone();
}

MediaPacketConsumerBase::SuppliedPacket::SuppliedPacket(
    uint64_t label,
    MediaPacketPtr packet,
    void* payload,
    const SupplyPacketCallback& callback,
    std::shared_ptr<SuppliedPacketCounter> counter)
    : label_(label),
      packet_(packet.Pass()),
      payload_(payload),
      callback_(callback),
      counter_(counter) {
  FTL_DCHECK(packet_);
  FTL_DCHECK(!callback.is_null());
  FTL_DCHECK(counter_);
  counter_->OnPacketArrival();
}

MediaPacketConsumerBase::SuppliedPacket::~SuppliedPacket() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  callback_.Run(counter_->OnPacketDeparture(label_));
}

MediaPacketConsumerBase::SuppliedPacketCounter::SuppliedPacketCounter(
    MediaPacketConsumerBase* owner)
    : owner_(owner) {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
}

MediaPacketConsumerBase::SuppliedPacketCounter::~SuppliedPacketCounter() {
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
}

}  // namespace media
}  // namespace mojo
