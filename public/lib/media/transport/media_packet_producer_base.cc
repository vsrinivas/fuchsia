// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/transport/media_packet_producer_base.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace media {

MediaPacketProducerBase::MediaPacketProducerBase()
    : allocator_(ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                 ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP) {
  // No demand initially.
  ResetDemand();
}

MediaPacketProducerBase::~MediaPacketProducerBase() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  // Reset the consumer first so all our callbacks get deleted.
  consumer_ = nullptr;
}

void MediaPacketProducerBase::SetFixedBufferSize(uint64_t size) {
  FXL_DCHECK(size > 0);
  allocator_.SetFixedBufferSize(size);
}

void MediaPacketProducerBase::Connect(
    fuchsia::media::MediaPacketConsumerPtr consumer,
    const fuchsia::media::MediaPacketProducer::ConnectCallback& callback) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(consumer);

  allocator_.Reset();

  consumer_ = std::move(consumer);
  consumer_.set_error_handler([this]() {
    consumer_.Unbind();
    OnFailure();
  });

  HandleDemandUpdate();
  callback();
}

void MediaPacketProducerBase::Disconnect() {
  consumer_.Unbind();
  ResetDemand();
}

void MediaPacketProducerBase::Reset() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  Disconnect();
  allocator_.Reset();
}

void MediaPacketProducerBase::FlushConsumer(
    bool hold_frame,
    fuchsia::media::MediaPacketConsumer::FlushCallback callback) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(consumer_.is_bound());

  {
    std::lock_guard<std::mutex> locker(mutex_);
    end_of_stream_ = false;
  }

  fuchsia::media::MediaPacketDemand demand;
  demand.min_packets_outstanding = 0;
  demand.min_pts = fuchsia::media::kNoTimestamp;
  UpdateDemand(demand);

  flush_in_progress_ = true;
  consumer_->Flush(hold_frame, [this, callback = std::move(callback)]() {
    flush_in_progress_ = false;
    callback();
  });
}

void* MediaPacketProducerBase::AllocatePayloadBuffer(size_t size) {
  return allocator_.AllocateRegion(size);
}

void MediaPacketProducerBase::ReleasePayloadBuffer(void* buffer) {
  allocator_.ReleaseRegion(buffer);
}

void MediaPacketProducerBase::ProducePacket(
    void* payload, size_t size, int64_t pts, TimelineRate pts_rate,
    bool keyframe, bool end_of_stream,
    fuchsia::media::MediaTypePtr revised_media_type,
    ProducePacketCallback callback) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(size == 0 || payload != nullptr);

  if (!consumer_.is_bound()) {
    callback();
    return;
  }

  SharedBufferSet::Locator locator = allocator_.LocatorFromPtr(payload);

  fuchsia::media::MediaPacket media_packet;
  media_packet.pts = pts;
  media_packet.pts_rate_ticks = pts_rate.subject_delta();
  media_packet.pts_rate_seconds = pts_rate.reference_delta();
  media_packet.flags = (keyframe ? fuchsia::media::kFlagKeyframe : 0) |
                       (end_of_stream ? fuchsia::media::kFlagEos : 0);
  media_packet.revised_media_type = std::move(revised_media_type);
  media_packet.payload_buffer_id = locator.buffer_id();
  media_packet.payload_offset = locator.offset();
  media_packet.payload_size = size;

  {
    std::lock_guard<std::mutex> locker(mutex_);
    ++packets_outstanding_;
    pts_last_produced_ = pts;
    end_of_stream_ = end_of_stream;
  }

  // Make sure the consumer is up-to-date with respect to buffers.
  uint32_t buffer_id;
  zx::vmo vmo;
  while (allocator_.PollForBufferUpdate(&buffer_id, &vmo)) {
    if (vmo) {
      consumer_->AddPayloadBuffer(buffer_id, std::move(vmo));
    } else {
      consumer_->RemovePayloadBuffer(buffer_id);
    }
  }

  consumer_->SupplyPacket(
      std::move(media_packet),
      fxl::MakeCopyable([this, callback = std::move(callback)](
                            fuchsia::media::MediaPacketDemandPtr demand) {
        FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

        {
          std::lock_guard<std::mutex> locker(mutex_);
          --packets_outstanding_;
        }

        if (demand) {
          UpdateDemand(*demand);
        }

        callback();
      }));
}

bool MediaPacketProducerBase::ShouldProducePacket(
    uint32_t additional_packets_outstanding) {
  std::lock_guard<std::mutex> locker(mutex_);

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
  return demand_.min_pts != fuchsia::media::kNoTimestamp &&
         demand_.min_pts > pts_last_produced_;
}

void MediaPacketProducerBase::OnFailure() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
}

void MediaPacketProducerBase::ResetDemand() {
  std::lock_guard<std::mutex> locker(mutex_);
  packets_outstanding_ = 0;
  demand_.min_packets_outstanding = 0;
  demand_.min_pts = fuchsia::media::kNoTimestamp;
}

void MediaPacketProducerBase::HandleDemandUpdate(
    fuchsia::media::MediaPacketDemandPtr demand) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  if (demand) {
    UpdateDemand(*demand);
  }

  if (consumer_.is_bound()) {
    consumer_->PullDemandUpdate(
        [this](fuchsia::media::MediaPacketDemandPtr demand) {
          FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
          HandleDemandUpdate(std::move(demand));
        });
  }
}

void MediaPacketProducerBase::UpdateDemand(
    const fuchsia::media::MediaPacketDemand& demand) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  if (flush_in_progress_) {
    // While flushing, we ignore demand changes, because the consumer may have
    // sent them before it knew we were flushing.
    return;
  }

  bool updated = false;

  {
    std::lock_guard<std::mutex> locker(mutex_);
    if (demand_.min_packets_outstanding != demand.min_packets_outstanding ||
        demand_.min_pts != demand.min_pts) {
      demand_.min_packets_outstanding = demand.min_packets_outstanding;
      demand_.min_pts = demand.min_pts;
      updated = true;
    }
  }

  if (updated) {
    OnDemandUpdated(demand.min_packets_outstanding, demand.min_pts);
  }
}

}  // namespace media
