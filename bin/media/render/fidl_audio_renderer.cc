// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/render/fidl_audio_renderer.h"

#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_rate.h"

#include "garnet/bin/media/framework/formatting.h"

using media::AudioRenderer2Ptr;
using media::AudioStreamType;
using media::AudioStreamTypeSet;
using media::PacketPtr;
using media::PayloadAllocator;
using media::Range;
using media::StreamType;
using media::StreamTypeSet;
using media::Timeline;
using media::TimelineRate;

namespace media_player {

// static
std::shared_ptr<FidlAudioRenderer> FidlAudioRenderer::Create(
    AudioRenderer2Ptr audio_renderer) {
  return std::make_shared<FidlAudioRenderer>(std::move(audio_renderer));
}

FidlAudioRenderer::FidlAudioRenderer(AudioRenderer2Ptr audio_renderer)
    : audio_renderer_(std::move(audio_renderer)), allocator_(0) {
  FXL_DCHECK(audio_renderer_);

  audio_renderer_->GetMinLeadTime([this](int64_t min_lead_time_nsec) {
    if (min_lead_time_nsec == 0) {
      FXL_LOG(WARNING)
          << "AudioRenderer2.GetMinLeadTime returned zero, ignoring.";
      return;
    }

    min_lead_time_ns_ = min_lead_time_nsec;
  });

  supported_stream_types_.push_back(AudioStreamTypeSet::Create(
      {StreamType::kAudioEncodingLpcm},
      AudioStreamType::SampleFormat::kUnsigned8,
      Range<uint32_t>(media::kMinChannelCount, media::kMaxChannelCount),
      Range<uint32_t>(media::kMinFramesPerSecond, media::kMaxFramesPerSecond)));

  supported_stream_types_.push_back(AudioStreamTypeSet::Create(
      {StreamType::kAudioEncodingLpcm},
      AudioStreamType::SampleFormat::kSigned16,
      Range<uint32_t>(media::kMinChannelCount, media::kMaxChannelCount),
      Range<uint32_t>(media::kMinFramesPerSecond, media::kMaxFramesPerSecond)));

  supported_stream_types_.push_back(AudioStreamTypeSet::Create(
      {StreamType::kAudioEncodingLpcm}, AudioStreamType::SampleFormat::kFloat,
      Range<uint32_t>(media::kMinChannelCount, media::kMaxChannelCount),
      Range<uint32_t>(media::kMinFramesPerSecond, media::kMaxFramesPerSecond)));
}

FidlAudioRenderer::~FidlAudioRenderer() {}

void FidlAudioRenderer::Flush(bool hold_frame_not_used) {
  flushed_ = true;
  last_supplied_pts_ = 0;
  SetEndOfStreamPts(media::kUnspecifiedTime);
  audio_renderer_->FlushNoReply();
}

Demand FidlAudioRenderer::SupplyPacket(PacketPtr packet) {
  FXL_DCHECK(packet);
  FXL_DCHECK(bytes_per_frame_ != 0);

  UpdateTimeline(Timeline::local_now());

  int64_t start_pts = packet->GetPts(pts_rate_);
  int64_t end_pts = start_pts + packet->size() / bytes_per_frame_;
  if (flushed_ || end_pts < from_ns(min_pts(0)) ||
      start_pts > from_ns(max_pts(0))) {
    // Discard this packet.
    return current_demand();
  }

  last_supplied_pts_ = end_pts;

  if (packet->end_of_stream()) {
    SetEndOfStreamPts(packet->GetPts(TimelineRate::NsPerSecond));

    if (prime_callback_) {
      // We won't get any more packets, so we're as primed as we're going to
      // get.
      prime_callback_();
      prime_callback_ = nullptr;
    }
  }

  if (packet->size() == 0) {
    packet.reset();
    UpdateTimeline(Timeline::local_now());
  } else {
    media::AudioPacket audioPacket;
    audioPacket.timestamp = start_pts;
    audioPacket.payload_offset = buffer_.OffsetFromPtr(packet->payload());
    audioPacket.payload_size = packet->size();

    audio_renderer_->SendPacket(audioPacket, [this, packet]() {
      UpdateTimeline(Timeline::local_now());
      stage()->SetDemand(current_demand());
    });
  }

  Demand demand = current_demand();

  if (prime_callback_ && demand == Demand::kNegative) {
    prime_callback_();
    prime_callback_ = nullptr;
  }

  return demand;
}

void FidlAudioRenderer::SetStreamType(const StreamType& stream_type) {
  FXL_DCHECK(stream_type.audio());

  media::AudioPcmFormat format;
  format.sample_format =
      fxl::To<media::AudioSampleFormat>(stream_type.audio()->sample_format());
  format.channels = stream_type.audio()->channels();
  format.frames_per_second = stream_type.audio()->frames_per_second();

  audio_renderer_->SetPcmFormat(std::move(format));

  // TODO: What about stream type changes?

  // Tell the allocator and buffer how large the buffer is.
  size_t size = stream_type.audio()->min_buffer_size(
      stream_type.audio()->frames_per_second());  // TODO How many seconds?
  buffer_.InitNew(size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE);
  allocator_.Reset(size);

  // Give the renderer a handle to the buffer vmo.
  audio_renderer_->SetPayloadBuffer(
      buffer_.GetDuplicateVmo(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP));

  // Tell the renderer that media time is in frames.
  audio_renderer_->SetPtsUnits(stream_type.audio()->frames_per_second(), 1);

  pts_rate_ = TimelineRate(stream_type.audio()->frames_per_second(), 1);
  bytes_per_frame_ = stream_type.audio()->bytes_per_frame();
}

void FidlAudioRenderer::Prime(fxl::Closure callback) {
  if (prime_callback_) {
    FXL_LOG(WARNING) << "Prime requested when priming was already in progress.";
    FXL_DCHECK(false);
    prime_callback_();
  }

  flushed_ = false;

  if (current_demand() == Demand::kNegative || end_of_stream_pending()) {
    callback();
    return;
  }

  prime_callback_ = callback;
  stage()->SetDemand(current_demand());
}

void FidlAudioRenderer::SetTimelineFunction(
    media::TimelineFunction timeline_function,
    fxl::Closure callback) {
  // AudioRenderer2 only supports 0/1 (paused) or 1/1 (normal playback rate).
  // TODO(dalesat): Remove this DCHECK when AudioRenderer2 supports other rates,
  // build an SRC into this class, or prohibit other rates entirely.
  FXL_DCHECK(timeline_function.subject_delta() == 0 ||
             (timeline_function.subject_delta() == 1 &&
              timeline_function.reference_delta() == 1));

  Renderer::SetTimelineFunction(timeline_function, callback);

  if (timeline_function.subject_delta() == 0) {
    audio_renderer_->PauseNoReply();
  } else {
    int64_t presentation_time = from_ns(timeline_function.subject_time());
    audio_renderer_->PlayNoReply(timeline_function.reference_time(),
                                 presentation_time);
  }

  UpdateTimelineAt(timeline_function.reference_time());
}

void FidlAudioRenderer::SetGain(float gain) {
  audio_renderer_->SetGainMuteNoReply(gain, false, 0);
}

void* FidlAudioRenderer::AllocatePayloadBuffer(size_t size) {
  FXL_DCHECK(size != 0);
  return buffer_.PtrFromOffset(allocator_.AllocateRegion(size));
}

void FidlAudioRenderer::ReleasePayloadBuffer(void* buffer) {
  FXL_DCHECK(buffer != nullptr);
  allocator_.ReleaseRegion(buffer_.OffsetFromPtr(buffer));
}

Demand FidlAudioRenderer::current_demand() {
  if (flushed_ || end_of_stream_pending()) {
    return Demand::kNegative;
  }

  int64_t presentation_time_ns =
      current_timeline_function()(Timeline::local_now());

  int64_t last_supplied_ns = to_ns(last_supplied_pts_);

  return (presentation_time_ns + min_lead_time_ns_ > last_supplied_ns)
             ? Demand::kPositive
             : Demand::kNegative;
}

}  // namespace media_player
