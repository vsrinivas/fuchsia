// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_capturer.h"

#include <lib/fit/defer.h>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

constexpr float kInitialCaptureGainDb = Gain::kUnityGainDb;

}

AudioCapturer::AudioCapturer(fuchsia::media::AudioCapturerConfiguration configuration,
                             std::optional<Format> format,
                             std::optional<fuchsia::media::AudioCaptureUsage> usage,
                             fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request,
                             Context* context)
    : BaseCapturer(std::move(configuration), std::move(format), std::move(request), context,
                   configuration.is_loopback() ? &RouteGraph::RemoveLoopbackCapturer
                                               : &RouteGraph::RemoveCapturer),
      loopback_(configuration.is_loopback()),
      mute_(false),
      stream_gain_db_(kInitialCaptureGainDb) {
  FX_DCHECK(context);
  context->volume_manager().AddStream(this);
  if (usage) {
    usage_ = *usage;
  }
}

AudioCapturer::~AudioCapturer() { context().volume_manager().RemoveStream(this); }

void AudioCapturer::ReportStart() {
  BaseCapturer::ReportStart();
  context().audio_admin().UpdateCapturerState(usage_, true, this);
}

void AudioCapturer::ReportStop() {
  BaseCapturer::ReportStop();
  context().audio_admin().UpdateCapturerState(usage_, false, this);
}

void AudioCapturer::OnStateChanged(State old_state, State new_state) {
  BaseCapturer::OnStateChanged(old_state, new_state);
  if (new_state == State::OperatingSync) {
    context().volume_manager().NotifyStreamChanged(this);
  }
}

void AudioCapturer::SetRoutingProfile(bool routable) {
  auto profile =
      RoutingProfile{.routable = routable, .usage = StreamUsage::WithCaptureUsage(usage_)};
  if (loopback_) {
    context().route_graph().SetLoopbackCapturerRoutingProfile(*this, std::move(profile));
  } else {
    context().route_graph().SetCapturerRoutingProfile(*this, std::move(profile));
  }
}

void AudioCapturer::OnLinkAdded() {
  BaseCapturer::OnLinkAdded();
  context().volume_manager().NotifyStreamChanged(this);
}

void AudioCapturer::SetPcmStreamType(fuchsia::media::AudioStreamType stream_type) {
  TRACE_DURATION("audio", "AudioCapturer::SetPcmStreamType");
  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fit::defer([this]() { BeginShutdown(); });

  // If our shared buffer has been assigned, we are operating and our mode can no longer be changed.
  State state = capture_state();
  if (state != State::WaitingForVmo) {
    FX_LOGS(ERROR) << "Cannot change capture mode while operating!"
                   << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  auto format_result = Format::Create(stream_type);
  if (format_result.is_error()) {
    FX_LOGS(ERROR) << "AudioCapturer: PcmStreamType is invalid";
    return;
  }

  REP(SettingCapturerStreamType(*this, stream_type));

  // Success, record our new format.
  UpdateFormat(format_result.take_value());

  cleanup.cancel();
}

void AudioCapturer::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) {
  TRACE_DURATION("audio", "AudioCapturer::BindGainControl");
  gain_control_bindings_.AddBinding(this, std::move(request));
}

void AudioCapturer::SetUsage(fuchsia::media::AudioCaptureUsage usage) {
  TRACE_DURATION("audio", "AudioCapturer::SetUsage");
  if (usage == usage_) {
    return;
  }

  ReportStop();
  usage_ = usage;
  context().volume_manager().NotifyStreamChanged(this);
  State state = capture_state();
  SetRoutingProfile(StateIsRoutable(state));
  if (state == State::OperatingAsync) {
    ReportStart();
  }
  if (state == State::OperatingSync) {
    if (has_pending_capture_buffers()) {
      ReportStart();
    }
  }
}

bool AudioCapturer::GetStreamMute() const { return mute_; }

fuchsia::media::Usage AudioCapturer::GetStreamUsage() const {
  fuchsia::media::Usage usage;
  usage.set_capture_usage(usage_);
  return usage;
}

void AudioCapturer::RealizeVolume(VolumeCommand volume_command) {
  if (volume_command.ramp.has_value()) {
    FX_LOGS(WARNING)
        << "Requested ramp of capturer; ramping for destination gains is unimplemented.";
  }

  context().link_matrix().ForEachSourceLink(*this,
                                            [this, &volume_command](LinkMatrix::LinkHandle link) {
                                              float gain_db = link.loudness_transform->Evaluate<3>({
                                                  VolumeValue{volume_command.volume},
                                                  GainDbFsValue{volume_command.gain_db_adjustment},
                                                  GainDbFsValue{stream_gain_db_.load()},
                                              });

                                              link.mixer->bookkeeping().gain.SetDestGain(gain_db);
                                            });
}

void AudioCapturer::SetGain(float gain_db) {
  TRACE_DURATION("audio", "AudioCapturer::SetGain");
  // Before setting stream_gain_db_, we should always perform this range check.
  if ((gain_db < fuchsia::media::audio::MUTED_GAIN_DB) ||
      (gain_db > fuchsia::media::audio::MAX_GAIN_DB) || isnan(gain_db)) {
    FX_LOGS(ERROR) << "SetGain(" << gain_db << " dB) out of range.";
    BeginShutdown();
    return;
  }

  // If the incoming SetGain request represents no change, we're done
  // (once we add gain ramping, this type of check isn't workable).
  if (stream_gain_db_ == gain_db) {
    return;
  }

  REP(SettingCapturerGain(*this, gain_db));

  stream_gain_db_.store(gain_db);
  context().volume_manager().NotifyStreamChanged(this);

  NotifyGainMuteChanged();
}

void AudioCapturer::SetMute(bool mute) {
  TRACE_DURATION("audio", "AudioCapturer::SetMute");
  // If the incoming SetMute request represents no change, we're done.
  if (mute_ == mute) {
    return;
  }

  REP(SettingCapturerMute(*this, mute));

  mute_ = mute;

  context().volume_manager().NotifyStreamChanged(this);
  NotifyGainMuteChanged();
}

void AudioCapturer::NotifyGainMuteChanged() {
  TRACE_DURATION("audio", "AudioCapturer::NotifyGainMuteChanged");
  // Consider making these events disable-able like MinLeadTime.
  for (auto& gain_binding : gain_control_bindings_.bindings()) {
    gain_binding->events().OnGainMuteChanged(stream_gain_db_, mute_);
  }
}

}  // namespace media::audio
