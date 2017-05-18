// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/lib/transport/media_packet_producer_base.h"

#include "lib/ftl/logging.h"

namespace media {

MediaPacketProducerBase::MediaPacketProducerBase()
    : allocator_(MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                 MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ |
                     MX_RIGHT_MAP) {
  // No demand initially.
  demand_.min_packets_outstanding = 0;
  demand_.min_pts = MediaPacket::kNoTimestamp;
}

MediaPacketProducerBase::~MediaPacketProducerBase() {
#ifndef NDEBUG
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
#endif
  // Reset the consumer first so all our callbacks get deleted.
  consumer_ = nullptr;
}

void MediaPacketProducerBase::Connect(
    MediaPacketConsumerPtr consumer,
    const MediaPacketProducer::ConnectCallback& callback) {
#ifndef NDEBUG
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
#endif
  FTL_DCHECK(consumer);

  FLOG(log_channel_, ConnectedTo(FLOG_PTR_KOID(consumer)));

  consumer_ = std::move(consumer);
  consumer_.set_connection_error_handler([this]() {
    consumer_.reset();
    OnFailure();
  });

  HandleDemandUpdate();
  callback();
}

void MediaPacketProducerBase::Reset() {
#ifndef NDEBUG
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
#endif
  FLOG(log_channel_, Resetting());
  Disconnect();
  allocator_.Reset();
}

void MediaPacketProducerBase::FlushConsumer(
    const MediaPacketConsumer::FlushCallback& callback) {
#ifndef NDEBUG
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
#endif
  FTL_DCHECK(consumer_.is_bound());

  FLOG(log_channel_, RequestingFlush());

  {
    ftl::MutexLocker locker(&mutex_);
    end_of_stream_ = false;
  }

  MediaPacketDemand demand;
  demand.min_packets_outstanding = 0;
  demand.min_pts = MediaPacket::kNoTimestamp;
  UpdateDemand(demand);

  flush_in_progress_ = true;
  consumer_->Flush([this, callback]() {
    flush_in_progress_ = false;
    FLOG(log_channel_, FlushCompleted());
    callback();
  });
}

void* MediaPacketProducerBase::AllocatePayloadBuffer(size_t size) {
  void* result = allocator_.AllocateRegion(size);
  if (result == nullptr) {
    FLOG(log_channel_, PayloadBufferAllocationFailure(0, size));
  } else {
    FLOG(log_channel_, AllocatingPayloadBuffer(0, size, FLOG_ADDRESS(result)));
  }
  return result;
}

void MediaPacketProducerBase::ReleasePayloadBuffer(void* buffer) {
  FLOG(log_channel_, ReleasingPayloadBuffer(0, FLOG_ADDRESS(buffer)));
  allocator_.ReleaseRegion(buffer);
}

void MediaPacketProducerBase::ProducePacket(
    void* payload,
    size_t size,
    int64_t pts,
    TimelineRate pts_rate,
    bool keyframe,
    bool end_of_stream,
    MediaTypePtr revised_media_type,
    const ProducePacketCallback& callback) {
#ifndef NDEBUG
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
#endif
  FTL_DCHECK(size == 0 || payload != nullptr);

  if (!consumer_.is_bound()) {
    callback();
    return;
  }

  SharedBufferSet::Locator locator = allocator_.LocatorFromPtr(payload);

  MediaPacketPtr media_packet = MediaPacket::New();
  media_packet->pts = pts;
  media_packet->pts_rate_ticks = pts_rate.subject_delta();
  media_packet->pts_rate_seconds = pts_rate.reference_delta();
  media_packet->keyframe = keyframe;
  media_packet->end_of_stream = end_of_stream;
  media_packet->revised_media_type = std::move(revised_media_type);
  media_packet->payload_buffer_id = locator.buffer_id();
  media_packet->payload_offset = locator.offset();
  media_packet->payload_size = size;

  uint32_t packets_outstanding;

  {
    ftl::MutexLocker locker(&mutex_);
    packets_outstanding = ++packets_outstanding_;
    pts_last_produced_ = pts;
    end_of_stream_ = end_of_stream;
  }

  uint64_t label = ++prev_packet_label_;

  FLOG(log_channel_,
       ProducingPacket(label, media_packet.Clone(), FLOG_ADDRESS(payload),
                       packets_outstanding));
  (void)packets_outstanding;  // Avoids 'unused' error in release builds.

  // Make sure the consumer is up-to-date with respect to buffers.
  uint32_t buffer_id;
  mx::vmo vmo;
  while (allocator_.PollForBufferUpdate(&buffer_id, &vmo)) {
    if (vmo) {
      consumer_->AddPayloadBuffer(buffer_id, std::move(vmo));
    } else {
      consumer_->RemovePayloadBuffer(buffer_id);
    }
  }

  consumer_->SupplyPacket(
      std::move(media_packet),
      [this, callback, label](MediaPacketDemandPtr demand) {
#ifndef NDEBUG
        FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
#endif

        uint32_t packets_outstanding;

        {
          ftl::MutexLocker locker(&mutex_);
          packets_outstanding = --packets_outstanding_;
        }

        FLOG(log_channel_, RetiringPacket(label, packets_outstanding));
        (void)packets_outstanding;  // Avoids 'unused' error in release builds.

        if (demand) {
          UpdateDemand(*demand);
        }

        callback();
      });
}

bool MediaPacketProducerBase::ShouldProducePacket(
    uint32_t additional_packets_outstanding) {
  ftl::MutexLocker locker(&mutex_);

  // Shouldn't send any more after end of stream.
  if (end_of_stream_) {
    return false;
  }

  // See if more packets are demanded.
  if (demand_.min_packets_outstanding >
      packets_outstanding_ + additional_packets_outstanding) {
    return true;
  }

  // See if a higher PTS is demanded.
  return demand_.min_pts != MediaPacket::kNoTimestamp &&
         demand_.min_pts > pts_last_produced_;
}

void MediaPacketProducerBase::OnFailure() {
#ifndef NDEBUG
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
#endif
}

void MediaPacketProducerBase::HandleDemandUpdate(MediaPacketDemandPtr demand) {
#ifndef NDEBUG
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
#endif
  if (demand) {
    UpdateDemand(*demand);
  }

  if (consumer_.is_bound()) {
    consumer_->PullDemandUpdate([this](MediaPacketDemandPtr demand) {
#ifndef NDEBUG
      FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
#endif
      HandleDemandUpdate(std::move(demand));
    });
  }
}

void MediaPacketProducerBase::UpdateDemand(const MediaPacketDemand& demand) {
#ifndef NDEBUG
  FTL_DCHECK(thread_checker_.IsCreationThreadCurrent());
#endif

  if (flush_in_progress_) {
    // While flushing, we ignore demand changes, because the consumer may have
    // sent them before it knew we were flushing.
    return;
  }

  bool updated = false;

  {
    ftl::MutexLocker locker(&mutex_);
    if (demand_.min_packets_outstanding != demand.min_packets_outstanding ||
        demand_.min_pts != demand.min_pts) {
      demand_.min_packets_outstanding = demand.min_packets_outstanding;
      demand_.min_pts = demand.min_pts;
      updated = true;
    }
  }

  if (updated) {
    FLOG(log_channel_, DemandUpdated(demand.Clone()));
    OnDemandUpdated(demand.min_packets_outstanding, demand.min_pts);
  }
}

}  // namespace media
