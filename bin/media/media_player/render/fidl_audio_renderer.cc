// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/render/fidl_audio_renderer.h"

#include <lib/async/default.h>

#include "garnet/bin/media/media_player/fidl/fidl_type_conversions.h"
#include "garnet/bin/media/media_player/framework/formatting.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media_player {
namespace {

constexpr int64_t kWarnThresholdNs = ZX_MSEC(500);

}  // namespace

// static
std::shared_ptr<FidlAudioRenderer> FidlAudioRenderer::Create(
    fuchsia::media::AudioRenderer2Ptr audio_renderer) {
  return std::make_shared<FidlAudioRenderer>(std::move(audio_renderer));
}

FidlAudioRenderer::FidlAudioRenderer(
    fuchsia::media::AudioRenderer2Ptr audio_renderer)
    : audio_renderer_(std::move(audio_renderer)),
      allocator_(0),
      arrivals_(true),
      departures_(false) {
  FXL_DCHECK(audio_renderer_);

  // |demand_task_| is used to wake up when demand might transition from
  // negative to positive.
  demand_task_.set_handler([this]() { SignalCurrentDemand(); });

  audio_renderer_.events().OnMinLeadTimeChanged =
      [this](int64_t min_lead_time_nsec) {
        FXL_DCHECK(async_get_default_dispatcher() == dispatcher());
        // Pad this number just a bit so we are sure to have time to get the
        // payloads delivered to the mixer over our channel.
        min_lead_time_nsec += ZX_MSEC(10);
        if (min_lead_time_nsec > min_lead_time_ns_) {
          min_lead_time_ns_ = min_lead_time_nsec;
        }
      };
  audio_renderer_->EnableMinLeadTimeEvents(true);

  supported_stream_types_.push_back(AudioStreamTypeSet::Create(
      {StreamType::kAudioEncodingLpcm},
      AudioStreamType::SampleFormat::kUnsigned8,
      Range<uint32_t>(fuchsia::media::MIN_PCM_CHANNEL_COUNT,
                      fuchsia::media::MAX_PCM_CHANNEL_COUNT),
      Range<uint32_t>(fuchsia::media::MIN_PCM_FRAMES_PER_SECOND,
                      fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)));

  supported_stream_types_.push_back(AudioStreamTypeSet::Create(
      {StreamType::kAudioEncodingLpcm},
      AudioStreamType::SampleFormat::kSigned16,
      Range<uint32_t>(fuchsia::media::MIN_PCM_CHANNEL_COUNT,
                      fuchsia::media::MAX_PCM_CHANNEL_COUNT),
      Range<uint32_t>(fuchsia::media::MIN_PCM_FRAMES_PER_SECOND,
                      fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)));

  supported_stream_types_.push_back(AudioStreamTypeSet::Create(
      {StreamType::kAudioEncodingLpcm}, AudioStreamType::SampleFormat::kFloat,
      Range<uint32_t>(fuchsia::media::MIN_PCM_CHANNEL_COUNT,
                      fuchsia::media::MAX_PCM_CHANNEL_COUNT),
      Range<uint32_t>(fuchsia::media::MIN_PCM_FRAMES_PER_SECOND,
                      fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)));
}

FidlAudioRenderer::~FidlAudioRenderer() {
  FXL_DCHECK(async_get_default_dispatcher() == dispatcher());
}

const char* FidlAudioRenderer::label() const { return "audio_renderer"; }

void FidlAudioRenderer::Dump(std::ostream& os) const {
  FXL_DCHECK(async_get_default_dispatcher() == dispatcher());
  Renderer::Dump(os);

  os << fostr::Indent;
  os << fostr::NewLine << "priming:               " << !!prime_callback_;
  os << fostr::NewLine << "flushed:               " << flushed_;
  os << fostr::NewLine << "presentation time:     "
     << AsNs(current_timeline_function()(media::Timeline::local_now()));
  os << fostr::NewLine
     << "last supplied pts:     " << AsNs(last_supplied_pts_ns_);
  os << fostr::NewLine
     << "last departed pts:     " << AsNs(last_departed_pts_ns_);
  os << fostr::NewLine << "supplied - departed:   "
     << AsNs(last_supplied_pts_ns_ - last_departed_pts_ns_);
  os << fostr::NewLine << "minimum lead time:     " << AsNs(min_lead_time_ns_);

  if (arrivals_.count() != 0) {
    os << fostr::NewLine << "packet arrivals: " << fostr::Indent << arrivals_
       << fostr::Outdent;
  }

  if (departures_.count() != 0) {
    os << fostr::NewLine << "packet departures: " << fostr::Indent
       << departures_ << fostr::Outdent;
  }

  os << fostr::Outdent;
}

void FidlAudioRenderer::FlushInput(bool hold_frame_not_used, size_t input_index,
                                   fit::closure callback) {
  FXL_DCHECK(async_get_default_dispatcher() == dispatcher());
  FXL_DCHECK(input_index == 0);
  FXL_DCHECK(callback);

  flushed_ = true;
  SetEndOfStreamPts(fuchsia::media::kUnspecifiedTime);
  input_packet_request_outstanding_ = false;

  audio_renderer_->Flush(
      fxl::MakeCopyable([this, callback = std::move(callback)]() {
        last_supplied_pts_ns_ = 0;
        last_departed_pts_ns_ = fuchsia::media::kUnspecifiedTime;
        callback();
      }));
}

void FidlAudioRenderer::PutInputPacket(PacketPtr packet, size_t input_index) {
  FXL_DCHECK(async_get_default_dispatcher() == dispatcher());
  FXL_DCHECK(packet);
  FXL_DCHECK(input_index == 0);
  FXL_DCHECK(bytes_per_frame_ != 0);

  input_packet_request_outstanding_ = false;

  int64_t now = media::Timeline::local_now();
  UpdateTimeline(now);

  int64_t start_pts = packet->GetPts(pts_rate_);
  int64_t start_pts_ns = to_ns(start_pts);
  int64_t end_pts_ns = to_ns(start_pts + packet->size() / bytes_per_frame_);

  if (flushed_ || end_pts_ns < min_pts(0) || start_pts_ns > max_pts(0)) {
    // Discard this packet.
    SignalCurrentDemand();
    return;
  }

  arrivals_.AddSample(now, current_timeline_function()(now), start_pts_ns,
                      Progressing());

  last_supplied_pts_ns_ = end_pts_ns;
  if (last_departed_pts_ns_ == fuchsia::media::kUnspecifiedTime) {
    last_departed_pts_ns_ = start_pts_ns;
  }

  if (packet->end_of_stream()) {
    SetEndOfStreamPts(start_pts_ns);
    if (current_timeline_function().invertable()) {
      // Make sure we wake up to signal end-of-stream when the time comes.
      UpdateTimelineAt(current_timeline_function().ApplyInverse(start_pts_ns));
    }

    if (prime_callback_) {
      // We won't get any more packets, so we're as primed as we're going to
      // get.
      prime_callback_();
      prime_callback_ = nullptr;
    }
  }

  if (packet->size() == 0) {
    packet.reset();
    UpdateTimeline(media::Timeline::local_now());
  } else {
    fuchsia::media::AudioPacket audioPacket;
    audioPacket.timestamp = start_pts;
    audioPacket.payload_size = packet->size();

    {
      std::lock_guard<std::mutex> locker(mutex_);
      audioPacket.payload_offset = buffer_.OffsetFromPtr(packet->payload());
    }

    audio_renderer_->SendPacket(audioPacket, [this, packet]() {
      FXL_DCHECK(async_get_default_dispatcher() == dispatcher());
      int64_t now = media::Timeline::local_now();

      UpdateTimeline(now);
      SignalCurrentDemand();

      int64_t pts_ns = packet->GetPts(media::TimelineRate::NsPerSecond);
      last_departed_pts_ns_ = std::max(pts_ns, last_departed_pts_ns_);

      departures_.AddSample(now, current_timeline_function()(now), pts_ns,
                            Progressing());
    });
  }

  if (SignalCurrentDemand()) {
    return;
  }

  if (prime_callback_) {
    // We have all the packets we need and we're priming. Signal that priming
    // is complete.
    prime_callback_();
    prime_callback_ = nullptr;
  }
}

void FidlAudioRenderer::SetStreamType(const StreamType& stream_type) {
  FXL_DCHECK(async_get_default_dispatcher() == dispatcher());
  FXL_DCHECK(stream_type.audio());

  fuchsia::media::AudioStreamType audio_stream_type;
  audio_stream_type.sample_format = fxl::To<fuchsia::media::AudioSampleFormat>(
      stream_type.audio()->sample_format());
  audio_stream_type.channels = stream_type.audio()->channels();
  audio_stream_type.frames_per_second =
      stream_type.audio()->frames_per_second();

  audio_renderer_->SetPcmStreamType(std::move(audio_stream_type));

  // TODO: What about stream type changes?

  // Tell the allocator and buffer how large the buffer is.
  size_t size = stream_type.audio()->min_buffer_size(
      stream_type.audio()->frames_per_second());  // TODO How many seconds?

  {
    std::lock_guard<std::mutex> locker(mutex_);
    buffer_.InitNew(size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE);
    allocator_.Reset(size);

    // Give the renderer a handle to the buffer vmo.
    audio_renderer_->SetPayloadBuffer(buffer_.GetDuplicateVmo(
        ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP));
  }

  // Tell the renderer that media time is in frames.
  audio_renderer_->SetPtsUnits(stream_type.audio()->frames_per_second(), 1);

  pts_rate_ = media::TimelineRate(stream_type.audio()->frames_per_second(), 1);
  bytes_per_frame_ = stream_type.audio()->bytes_per_frame();
}

void FidlAudioRenderer::Prime(fit::closure callback) {
  FXL_DCHECK(async_get_default_dispatcher() == dispatcher());

  if (prime_callback_) {
    FXL_LOG(WARNING) << "Prime requested when priming was already in progress.";
    FXL_DCHECK(false);
    prime_callback_();
  }

  flushed_ = false;

  if (!NeedMorePackets() || end_of_stream_pending()) {
    callback();
    return;
  }

  prime_callback_ = std::move(callback);
  SignalCurrentDemand();
}

void FidlAudioRenderer::SetTimelineFunction(
    media::TimelineFunction timeline_function, fit::closure callback) {
  FXL_DCHECK(async_get_default_dispatcher() == dispatcher());
  // AudioRenderer only supports 0/1 (paused) or 1/1 (normal playback rate).
  // TODO(dalesat): Remove this DCHECK when AudioRenderer supports other rates,
  // build an SRC into this class, or prohibit other rates entirely.
  FXL_DCHECK(timeline_function.subject_delta() == 0 ||
             (timeline_function.subject_delta() == 1 &&
              timeline_function.reference_delta() == 1));

  Renderer::SetTimelineFunction(timeline_function, std::move(callback));

  if (timeline_function.subject_delta() == 0) {
    audio_renderer_->PauseNoReply();
  } else {
    int64_t presentation_time = from_ns(timeline_function.subject_time());
    audio_renderer_->PlayNoReply(timeline_function.reference_time(),
                                 presentation_time);
  }
}

void FidlAudioRenderer::SetGain(float gain) {
  FXL_DCHECK(async_get_default_dispatcher() == dispatcher());
  audio_renderer_->SetGainMuteNoReply(gain, false, 0);
}

void* FidlAudioRenderer::AllocatePayloadBuffer(size_t size) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(size != 0);
  std::lock_guard<std::mutex> locker(mutex_);
  return buffer_.PtrFromOffset(allocator_.AllocateRegion(size));
}

void FidlAudioRenderer::ReleasePayloadBuffer(void* buffer) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(buffer != nullptr);
  std::lock_guard<std::mutex> locker(mutex_);
  allocator_.ReleaseRegion(buffer_.OffsetFromPtr(buffer));
}

void FidlAudioRenderer::OnTimelineTransition() {
  FXL_DCHECK(async_get_default_dispatcher() == dispatcher());

  if (end_of_stream_pending() && current_timeline_function().invertable()) {
    // Make sure we wake up to signal end-of-stream when the time comes.
    UpdateTimelineAt(
        current_timeline_function().ApplyInverse(end_of_stream_pts()));
  }
}

bool FidlAudioRenderer::NeedMorePackets() {
  FXL_DCHECK(async_get_default_dispatcher() == dispatcher());

  demand_task_.Cancel();

  if (flushed_ || end_of_stream_pending()) {
    // If we're flushed or we've seen end of stream, we don't need any more
    // packets.
    return false;
  }

  int64_t presentation_time_ns =
      current_timeline_function()(media::Timeline::local_now());

  if (presentation_time_ns + min_lead_time_ns_ > last_supplied_pts_ns_) {
    // We need more packets to meet lead time commitments.
    if (last_departed_pts_ns_ != fuchsia::media::kUnspecifiedTime &&
        last_supplied_pts_ns_ - last_departed_pts_ns_ > kWarnThresholdNs) {
      FXL_LOG(WARNING) << "Audio renderer holding too much content:";
      FXL_LOG(WARNING) << "    total content "
                       << AsNs(last_supplied_pts_ns_ - last_departed_pts_ns_);
      FXL_LOG(WARNING) << "    arrivals lead pts by "
                       << AsNs(last_supplied_pts_ns_ - presentation_time_ns);
      FXL_LOG(WARNING) << "    departures trail pts by "
                       << AsNs(presentation_time_ns - last_departed_pts_ns_);
    }

    return true;
  }

  if (!current_timeline_function().invertable()) {
    // We don't need packets now, and the timeline isn't progressing, so we
    // won't need packets until the timeline starts progressing.
    return false;
  }

  // We don't need packets now. Predict when we might need the next packet
  // and check then.
  demand_task_.PostForTime(dispatcher(),
                           zx::time(current_timeline_function().ApplyInverse(
                               last_supplied_pts_ns_ - min_lead_time_ns_)));

  return false;
}

bool FidlAudioRenderer::SignalCurrentDemand() {
  FXL_DCHECK(async_get_default_dispatcher() == dispatcher());

  if (input_packet_request_outstanding_) {
    return false;
  }

  if (!NeedMorePackets()) {
    return false;
  }

  input_packet_request_outstanding_ = true;
  stage()->RequestInputPacket();
  return true;
}

}  // namespace media_player
