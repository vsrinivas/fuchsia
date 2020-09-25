// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/fidl/fidl_audio_renderer.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>

#include "lib/async/default.h"
#include "lib/fostr/indent.h"
#include "lib/media/cpp/timeline_rate.h"
#include "lib/trace-engine/types.h"
#include "lib/trace/internal/event_common.h"
#include "src/media/playback/mediaplayer/fidl/fidl_type_conversions.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"

namespace media_player {
namespace {

constexpr int64_t kDefaultMinLeadTime = ZX_MSEC(100);
constexpr int64_t kTargetLeadTimeDeltaNs = ZX_MSEC(10);
constexpr int64_t kNoPtsSlipOnStarveNs = ZX_MSEC(500);
constexpr uint32_t kPayloadVmoSizeInSeconds = 1;

}  // namespace

// static
std::shared_ptr<FidlAudioRenderer> FidlAudioRenderer::Create(
    fuchsia::media::AudioRendererPtr audio_renderer) {
  return std::make_shared<FidlAudioRenderer>(std::move(audio_renderer));
}

FidlAudioRenderer::FidlAudioRenderer(fuchsia::media::AudioRendererPtr audio_renderer)
    : audio_renderer_(std::move(audio_renderer)), arrivals_(true), departures_(false) {
  FX_DCHECK(audio_renderer_);

  // |demand_task_| is used to wake up when demand might transition from
  // negative to positive.
  demand_task_.set_handler([this]() {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    SignalCurrentDemand();
  });

  min_lead_time_ns_ = kDefaultMinLeadTime;
  target_lead_time_ns_ = min_lead_time_ns_ + kTargetLeadTimeDeltaNs;

  audio_renderer_.set_error_handler([](zx_status_t status) {
    if (status != ZX_ERR_CANCELED) {
      // TODO(dalesat): Report this to the graph.
      FX_PLOGS(FATAL, status) << "AudioRenderer connection closed.";
    }
  });

  audio_renderer_.events().OnMinLeadTimeChanged = [this](int64_t min_lead_time_ns) {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    renderer_responding_ = true;

    if (min_lead_time_ns == 0) {
      // Ignore the zero we get during warmup.
      // TODO(dalesat): Remove check when fxbug.dev/13525 is fixed.
      return;
    }

    // Target lead time is somewhat greater than minimum lead time, so
    // we stay slightly ahead of the deadline.
    min_lead_time_ns_ = min_lead_time_ns;
    target_lead_time_ns_ = min_lead_time_ns_ + kTargetLeadTimeDeltaNs;
  };
  audio_renderer_->EnableMinLeadTimeEvents(true);

  supported_stream_types_.push_back(AudioStreamTypeSet::Create(
      {StreamType::kAudioEncodingLpcm}, AudioStreamType::SampleFormat::kUnsigned8,
      Range<uint32_t>(fuchsia::media::MIN_PCM_CHANNEL_COUNT, fuchsia::media::MAX_PCM_CHANNEL_COUNT),
      Range<uint32_t>(fuchsia::media::MIN_PCM_FRAMES_PER_SECOND,
                      fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)));

  supported_stream_types_.push_back(AudioStreamTypeSet::Create(
      {StreamType::kAudioEncodingLpcm}, AudioStreamType::SampleFormat::kSigned16,
      Range<uint32_t>(fuchsia::media::MIN_PCM_CHANNEL_COUNT, fuchsia::media::MAX_PCM_CHANNEL_COUNT),
      Range<uint32_t>(fuchsia::media::MIN_PCM_FRAMES_PER_SECOND,
                      fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)));

  supported_stream_types_.push_back(AudioStreamTypeSet::Create(
      {StreamType::kAudioEncodingLpcm}, AudioStreamType::SampleFormat::kFloat,
      Range<uint32_t>(fuchsia::media::MIN_PCM_CHANNEL_COUNT, fuchsia::media::MAX_PCM_CHANNEL_COUNT),
      Range<uint32_t>(fuchsia::media::MIN_PCM_FRAMES_PER_SECOND,
                      fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)));
}

FidlAudioRenderer::~FidlAudioRenderer() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  audio_renderer_.set_error_handler(nullptr);
}

const char* FidlAudioRenderer::label() const { return "audio_renderer"; }

void FidlAudioRenderer::Dump(std::ostream& os) const {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  Renderer::Dump(os);

  os << fostr::Indent;
  os << fostr::NewLine << "priming:               " << !!prime_callback_;
  os << fostr::NewLine << "flushed:               " << flushed_;
  os << fostr::NewLine << "presentation time:     "
     << AsNs(current_timeline_function()(zx::clock::get_monotonic().get()));
  os << fostr::NewLine << "last supplied pts:     " << AsNs(last_supplied_pts_ns_);
  os << fostr::NewLine << "last departed pts:     " << AsNs(last_departed_pts_ns_);
  if (last_supplied_pts_ns_ != Packet::kNoPts && last_departed_pts_ns_ != Packet::kNoPts) {
    os << fostr::NewLine
       << "supplied - departed:   " << AsNs(last_supplied_pts_ns_ - last_departed_pts_ns_);
  }

  os << fostr::NewLine << "packet bytes out:      " << packet_bytes_outstanding_;
  os << fostr::NewLine << "minimum lead time:     " << AsNs(min_lead_time_ns_);

  if (arrivals_.count() != 0) {
    os << fostr::NewLine << "packet arrivals: " << fostr::Indent << arrivals_ << fostr::Outdent;
  }

  if (departures_.count() != 0) {
    os << fostr::NewLine << "packet departures: " << fostr::Indent << departures_ << fostr::Outdent;
  }

  os << fostr::Outdent;
}

void FidlAudioRenderer::OnInputConnectionReady(size_t input_index) {
  FX_DCHECK(input_index == 0);

  auto vmos = UseInputVmos().GetVmos();
  FX_DCHECK(vmos.size() == 1);
  audio_renderer_->AddPayloadBuffer(
      0, vmos.front()->Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP));

  payload_buffer_size_ = vmos.front()->size();

  input_connection_ready_ = true;

  if (when_input_connection_ready_) {
    when_input_connection_ready_();
    when_input_connection_ready_ = nullptr;
  }
}

void FidlAudioRenderer::FlushInput(bool hold_frame_not_used, size_t input_index,
                                   fit::closure callback) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FX_DCHECK(input_index == 0);
  FX_DCHECK(callback);

  flushed_ = true;
  SetEndOfStreamPts(Packet::kNoPts);
  UpdateLastRenderedPts(Packet::kNoPts);
  input_packet_request_outstanding_ = false;

  // Doing this here just to be safe. In theory, we should be tracking this value correctly
  // regardless of flush. See |PushPacket| below.
  packet_bytes_outstanding_ = 0;
  expected_packet_size_ = 0;

  audio_renderer_->DiscardAllPackets([this, callback = std::move(callback)]() {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    last_supplied_pts_ns_ = Packet::kNoPts;
    last_departed_pts_ns_ = Packet::kNoPts;
    callback();
  });
}

void FidlAudioRenderer::PutInputPacket(PacketPtr packet, size_t input_index) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FX_DCHECK(packet);
  FX_DCHECK(input_index == 0);
  FX_DCHECK(bytes_per_frame_ != 0);

  input_packet_request_outstanding_ = false;

  int64_t now = zx::clock::get_monotonic().get();

  TRACE_DURATION("mediaplayer:render", "PutInputPacket", "pts", packet->pts());

  if (packet->pts() == Packet::kNoPts) {
    if (!renderer_responding_) {
      // Discard this packet.
      SignalCurrentDemand();
      return;
    }

    // The packet has no PTS. We need to assign one. We prefer to use frame
    // units, so first make sure the PTS rate is set to frames.
    // TODO(dalesat): Remove this code when fxbug.dev/13524 is fixed.
    packet->SetPtsRate(pts_rate_);

    if (next_pts_to_assign_ == Packet::kNoPts || packet->discontinuity()) {
      // No PTS has been established. Set the PTS so we get the target lead time, which is somewhat
      // greater than minimum lead time.
      int64_t new_pts = from_ns(current_timeline_function()(now) + target_lead_time_ns_);
      TRACE_INSTANT("mediaplayer:render", "no_pts", TRACE_SCOPE_THREAD, "pts", new_pts);
      packet->SetPts(new_pts);
    } else {
      int64_t min_pts = from_ns(current_timeline_function()(now) + target_lead_time_ns_);
      if (next_pts_to_assign_ < min_pts) {
        // Packet has arrived too late to be rendered. Slip the PTS into the future so we aren't
        // starving anymore. If the overall arrival rate of packets is too low, this will happen
        // repeatedly.
        int64_t new_pts = from_ns(current_timeline_function()(now) + kNoPtsSlipOnStarveNs);
        FX_LOGS(WARNING) << "Packets without timestamps arriving too infrequently, inserting "
                         << to_ns(new_pts - next_pts_to_assign_) / ZX_MSEC(1) << "ms of silence.";

        packet->SetPts(new_pts);

        TRACE_INSTANT("mediaplayer:render", "missed", TRACE_SCOPE_THREAD, "pts",
                      next_pts_to_assign_, "now", min_pts, "min", new_pts);
      } else {
        // Set the packet's PTS to immediately follow the previous packet.
        packet->SetPts(next_pts_to_assign_);
      }
    }
  }

  int64_t start_pts = packet->GetPts(pts_rate_);
  int64_t start_pts_ns = to_ns(start_pts);

  next_pts_to_assign_ = start_pts + packet->size() / bytes_per_frame_;

  last_supplied_pts_ns_ = to_ns(next_pts_to_assign_);

  if (last_departed_pts_ns_ == Packet::kNoPts) {
    last_departed_pts_ns_ = start_pts_ns;
  }

  if (flushed_ || last_supplied_pts_ns_ < min_pts(0) || start_pts_ns > max_pts(0)) {
    // Discard this packet.
    SignalCurrentDemand();
    return;
  }

  arrivals_.AddSample(now, current_timeline_function()(now), start_pts_ns, Progressing());

  if (packet->end_of_stream()) {
    SetEndOfStreamPts(last_supplied_pts_ns_);

    if (prime_callback_) {
      // We won't get any more packets, so we're as primed as we're going to
      // get.
      prime_callback_();
      prime_callback_ = nullptr;
    }
  }

  if (packet->size() == 0 || unsupported_rate_) {
    // Don't send the packet if it's zero-sized or the current rate isn't supported.
    packet = nullptr;
    if (unsupported_rate_) {
      // Needed to enure end-of-stream is notified.
      UpdateLastRenderedPts(start_pts_ns);
    }
  } else {
    fuchsia::media::StreamPacket audioPacket;
    audioPacket.pts = start_pts;
    audioPacket.payload_size = packet->size();
    audioPacket.payload_offset = packet->payload_buffer()->offset();
    audioPacket.flags =
        packet->discontinuity() ? fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY : 0;

    packet_bytes_outstanding_ += packet->size();

    // Expect the next packet to be the same size as the current one. This is just a guess, of
    // course, but likely to be the case for most decoders/demuxes.
    expected_packet_size_ = packet->size();

    audio_renderer_->SendPacket(audioPacket, [this, packet]() {
      FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
      int64_t now = zx::clock::get_monotonic().get();

      int64_t start_pts = packet->GetPts(pts_rate_);
      int64_t start_pts_ns = to_ns(start_pts);
      int64_t end_pts_ns = to_ns(start_pts + packet->size() / bytes_per_frame_);

      UpdateLastRenderedPts(end_pts_ns);

      last_departed_pts_ns_ = std::max(end_pts_ns, last_departed_pts_ns_);

      // We do this check, because |packet_bytes_outstanding_| is cleared in |FlushInput|. This
      // approach probably isn't needed, but makes it less likely that |packet_bytes_outstanding_|
      // will wander off into bogus territory.
      if (packet_bytes_outstanding_ < packet->size()) {
        packet_bytes_outstanding_ = 0;
      } else {
        packet_bytes_outstanding_ -= packet->size();
      }

      departures_.AddSample(now, current_timeline_function()(now), start_pts_ns, Progressing());

      SignalCurrentDemand();
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
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FX_DCHECK(stream_type.audio());

  fuchsia::media::AudioStreamType audio_stream_type;
  audio_stream_type.sample_format =
      fidl::To<fuchsia::media::AudioSampleFormat>(stream_type.audio()->sample_format());
  audio_stream_type.channels = stream_type.audio()->channels();
  audio_stream_type.frames_per_second = stream_type.audio()->frames_per_second();

  audio_renderer_->SetPcmStreamType(audio_stream_type);

  // TODO: What about stream type changes?

  // Configure the input for a single VMO of adequate size.
  size_t size = stream_type.audio()->min_buffer_size(stream_type.audio()->frames_per_second() *
                                                     kPayloadVmoSizeInSeconds);

  ConfigureInputToUseVmos(size,  // max_aggregate_payload_size
                          0,     // max_payload_count
                          0,     // max_payload_size
                          VmoAllocation::kSingleVmo);

  // Tell the renderer that media time is in frames.
  audio_renderer_->SetPtsUnits(stream_type.audio()->frames_per_second(), 1);

  pts_rate_ = media::TimelineRate(stream_type.audio()->frames_per_second(), 1);
  bytes_per_frame_ = stream_type.audio()->bytes_per_frame();
}

void FidlAudioRenderer::Prime(fit::closure callback) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  if (prime_callback_) {
    FX_LOGS(WARNING) << "Prime requested when priming was already in progress.";
    // This used to be a DCHECK but for the use case of AudioConsumer we should allow new sources to
    // be attached without an end of stream occuring and clearing the prime_callback
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

void FidlAudioRenderer::SetTimelineFunction(media::TimelineFunction timeline_function,
                                            fit::closure callback) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  // AudioRenderer only fully supports 0/1 (paused) or 1/1 (normal playback rate). If the
  // playback rate isn't 1/1, packets are discarded rather than being renderered. This means
  // that if the |SetPlaybackRate| method is used on the player to set a rate other than 1.0,
  // the audio portion of the content will not be heard.

  when_input_connection_ready_ = [this, timeline_function,
                                  callback = std::move(callback)]() mutable {
    Renderer::SetTimelineFunction(timeline_function, std::move(callback));

    if (timeline_function.subject_delta() == 0) {
      audio_renderer_->PauseNoReply();
    } else {
      int64_t presentation_time = from_ns(timeline_function.subject_time());
      audio_renderer_->PlayNoReply(timeline_function.reference_time(), presentation_time);
    }
  };

  if (input_connection_ready_) {
    when_input_connection_ready_();
    when_input_connection_ready_ = nullptr;
  }
}

void FidlAudioRenderer::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::audio::GainControl> gain_control_request) {
  audio_renderer_->BindGainControl(std::move(gain_control_request));
}

void FidlAudioRenderer::OnTimelineTransition() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  auto& timeline = current_timeline_function();
  unsupported_rate_ =
      timeline.subject_delta() != 0 && timeline.subject_delta() != timeline.reference_delta();
  if (unsupported_rate_) {
    audio_renderer_->DiscardAllPackets([]() {});
  }

  SignalCurrentDemand();
}

bool FidlAudioRenderer::NeedMorePackets() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  demand_task_.Cancel();

  if (flushed_ || end_of_stream_pending()) {
    // If we're flushed or we've seen end of stream, we don't need any more
    // packets.
    return false;
  }

  if (packet_bytes_outstanding_ + expected_packet_size_ >= payload_buffer_size_) {
    // Packets aren't getting retired quickly enough, and the next packet is likely to exceed
    // the capacity of the payload VMO. We'll refrain from requesting another packet at the
    // risk of failing to meet lead time commitments. This is unlikely to happen on a target
    // with real hardware, but happens from time to time in automated test on emulators.
    if (!stall_logged_) {
      FX_LOGS(WARNING) << "Audio stalled, because the renderer is not retiring packets";
      stall_logged_ = true;
    }

    return false;
  }

  stall_logged_ = false;

  int64_t presentation_time_ns = current_timeline_function()(zx::clock::get_monotonic().get());

  if (last_supplied_pts_ns_ == Packet::kNoPts ||
      presentation_time_ns + target_lead_time_ns_ > last_supplied_pts_ns_) {
    // We need more packets to meet lead time commitments.
    return true;
  }

  if (!current_timeline_function().invertible()) {
    // We don't need packets now, and the timeline isn't progressing, so we
    // won't need packets until the timeline starts progressing.
    return false;
  }

  // We don't need packets now. Predict when we might need the next packet
  // and check then.
  demand_task_.PostForTime(dispatcher(), zx::time(current_timeline_function().ApplyInverse(
                                             last_supplied_pts_ns_ - target_lead_time_ns_)));
  return false;
}

bool FidlAudioRenderer::SignalCurrentDemand() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  if (input_packet_request_outstanding_) {
    return false;
  }

  if (!NeedMorePackets()) {
    return false;
  }

  input_packet_request_outstanding_ = true;
  RequestInputPacket();
  return true;
}

}  // namespace media_player
