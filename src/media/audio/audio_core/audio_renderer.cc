// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_renderer.h"

#include <fuchsia/media/audio/cpp/fidl.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/audio_core/stream_usage.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/lib/clock/utils.h"

namespace media::audio {

AudioRenderer::AudioRenderer(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request, Context* context)
    : BaseRenderer(std::move(audio_renderer_request), context) {
  context->volume_manager().AddStream(this);
  reporter().SetUsage(RenderUsageFromFidlRenderUsage(usage_));
}

AudioRenderer::~AudioRenderer() {
  // We (not ~BaseRenderer) must call this, because our ReportStop is gone when parent dtor runs
  ReportStopIfStarted();

  context().volume_manager().RemoveStream(this);
}

void AudioRenderer::OnLinkAdded() {
  // With a link, our Mixer and Gain objects have been created, so we can set initial gain levels.
  context().volume_manager().NotifyStreamChanged(this);

  BaseRenderer::OnLinkAdded();
}

void AudioRenderer::ReportStart() {
  BaseRenderer::ReportStart();
  context().audio_admin().UpdateRendererState(RenderUsageFromFidlRenderUsage(usage_), true, this);
}

void AudioRenderer::ReportStop() {
  BaseRenderer::ReportStop();
  context().audio_admin().UpdateRendererState(RenderUsageFromFidlRenderUsage(usage_), false, this);
}

void AudioRenderer::SetUsage(fuchsia::media::AudioRenderUsage usage) {
  TRACE_DURATION("audio", "AudioRenderer::SetUsage");
  if (format_) {
    FX_LOGS(WARNING) << "SetUsage called after SetPcmStreamType.";
    context().route_graph().RemoveRenderer(*this);
    return;
  }
  reporter().SetUsage(RenderUsageFromFidlRenderUsage(usage));
  usage_ = usage;
}

// If received clock is null, use our adjustable clock. Else, use this new clock. Fail/disconnect,
// if the client-submitted clock has insufficient rights. Strip off other rights such as WRITE.
void AudioRenderer::SetReferenceClock(zx::clock ref_clock) {
  TRACE_DURATION("audio", "AudioRenderer::SetReferenceClock");
  auto cleanup = fit::defer([this]() { context().route_graph().RemoveRenderer(*this); });

  // Lock after storing |cleanup| to ensure |mutex_| is released upon function return, rather than
  // |cleanup| completion.
  std::lock_guard<std::mutex> lock(mutex_);

  // We cannot change the reference clock, once it is set. Also, calling `SetPcmStreamType` will
  // automatically sets the default reference clock, if one has not been explicitly set.
  if (reference_clock_is_set_) {
    FX_LOGS(WARNING) << "Attempted to change reference clock after setting it.";
    return;
  }

  zx_status_t status;
  if (ref_clock.is_valid()) {
    status = SetCustomReferenceClock(std::move(ref_clock));
  } else {
    status = SetAdjustableReferenceClock();
  }
  if (status != ZX_OK) {
    return;
  }

  reference_clock_is_set_ = true;

  cleanup.cancel();
}

void AudioRenderer::SetPcmStreamType(fuchsia::media::AudioStreamType stream_type) {
  TRACE_DURATION("audio", "AudioRenderer::SetPcmStreamType");
  std::lock_guard<std::mutex> lock(mutex_);

  auto cleanup = fit::defer([this]() { context().route_graph().RemoveRenderer(*this); });

  // We cannot change the format while we are currently operational
  if (IsOperating()) {
    FX_LOGS(WARNING) << "Attempted to set format while in operational mode.";
    return;
  }

  auto format_result = Format::Create(stream_type);
  if (format_result.is_error()) {
    FX_LOGS(WARNING) << "AudioRenderer: PcmStreamType is invalid";
    return;
  }
  format_ = {format_result.take_value()};

  reporter().SetFormat(*format_);

  context().route_graph().SetRendererRoutingProfile(
      *this, {.routable = true, .usage = StreamUsage::WithRenderUsage(usage_)});

  // Once we route the renderer, we accept the default reference clock if one hasn't yet been set.
  reference_clock_is_set_ = true;

  // Things went well, cancel the cleanup hook. If our config had been validated previously, it will
  // have to be revalidated as we move into the operational phase of our life.
  InvalidateConfiguration();
  cleanup.cancel();
}

void AudioRenderer::SerializeWithPause(fit::closure callback) {
  if (pause_ramp_state_) {
    pause_ramp_state_->queued.push_back(std::move(callback));
  } else {
    callback();
  }
}

void AudioRenderer::AddPayloadBufferInternal(uint32_t id, zx::vmo payload_buffer) {
  SerializeWithPause([this, id, payload_buffer = std::move(payload_buffer)]() mutable {
    BaseRenderer::AddPayloadBufferInternal(id, std::move(payload_buffer));
  });
}

void AudioRenderer::RemovePayloadBufferInternal(uint32_t id) {
  SerializeWithPause([this, id]() mutable { BaseRenderer::RemovePayloadBufferInternal(id); });
}

void AudioRenderer::SendPacketInternal(fuchsia::media::StreamPacket packet,
                                       SendPacketCallback callback) {
  SerializeWithPause([this, packet, callback = std::move(callback)]() mutable {
    BaseRenderer::SendPacketInternal(packet, std::move(callback));
  });
}

void AudioRenderer::DiscardAllPacketsInternal(DiscardAllPacketsCallback callback) {
  SerializeWithPause([this, callback = std::move(callback)]() mutable {
    BaseRenderer::DiscardAllPacketsInternal(std::move(callback));
  });
}

void AudioRenderer::EnableMinLeadTimeEventsInternal(bool enabled) {
  SerializeWithPause(
      [this, enabled]() mutable { BaseRenderer::EnableMinLeadTimeEventsInternal(enabled); });
}

void AudioRenderer::GetMinLeadTimeInternal(GetMinLeadTimeCallback callback) {
  SerializeWithPause([this, callback = std::move(callback)]() mutable {
    BaseRenderer::GetMinLeadTimeInternal(std::move(callback));
  });
}

// To eliminate audible pops from discontinuity-on-immediate-start, ramp up from a very low level.
constexpr bool kEnableRampUpOnPlay = true;
constexpr float kInitialRampUpGainDb = -120.0f;
constexpr zx::duration kRampUpOnPlayDuration = zx::msec(5);

// To eliminate audible pops from discontinuity-on-pause, first ramp down to silence, then pause.
constexpr bool kEnableRampDownOnPause = true;
constexpr float kFinalRampDownGainDb = -120.0f;
constexpr zx::duration kRampDownOnPauseDuration = zx::msec(5);

void AudioRenderer::PlayInternal(zx::time reference_time, zx::time media_time,
                                 PlayCallback callback) {
  if constexpr (kEnableRampDownOnPause) {
    // Allow Play() to interrupt a pending Pause(). This reduces the chance of underflow when
    // the client calls Play() with a reference_time very close to now -- if we instead wait
    // for the Pause() to complete before calling Play(), we delay starting the Play(), which
    // may move the clock past reference_time.
    if (pause_ramp_state_) {
      FinishPauseRamp(pause_ramp_state_);
    }
  }

  if constexpr (kEnableRampUpOnPlay) {
    // As a workaround until time-stamped Play/Pause/Gain commands, start a ramp-up then call Play.
    // Set gain to silent, before starting the ramp-up to current val.
    PostStreamGainMute({kInitialRampUpGainDb,
                        GainRamp{stream_gain_db_, kRampUpOnPlayDuration,
                                 fuchsia::media::audio::RampType::SCALE_LINEAR},
                        std::nullopt});
  }

  BaseRenderer::PlayInternal(reference_time, media_time, std::move(callback));
}

void AudioRenderer::PauseInternal(PauseCallback callback) {
  if constexpr (!kEnableRampDownOnPause) {
    BaseRenderer::PauseInternal(std::move(callback));
    return;
  }

  // If already pausing, just queue this callback to be run when the pause ramp completes.
  // There cannot be an intervening Play() because Play() always interrupts the pause ramp.
  if (pause_ramp_state_) {
    if (callback) {
      pause_ramp_state_->callbacks.push_back(std::move(callback));
    }
    return;
  }

  // As a short-term workaround until time-stamped Play/Pause/Gain commands are in place, start the
  // ramp-down immediately, and post a delayed task for the actual Pause.
  // On receiving the Pause callback, restore stream gain to its original value.
  pause_ramp_state_ = std::make_shared<PauseRampState>();
  pause_ramp_state_->prior_stream_gain_db = stream_gain_db_;
  if (callback) {
    pause_ramp_state_->callbacks.push_back(std::move(callback));
  }

  // Callback to tear down pause_ramp_state_ when the ramp completes.
  // We add a shared self-reference in case the renderer is unbound before this callback runs.
  auto finish_pause_ramp = [this, self = shared_from_this(), state = pause_ramp_state_]() mutable {
    FinishPauseRamp(state);
  };

  // Use internal SetGain/SetGainWithRamp versions, to avoid gain notifications.
  PostStreamGainMute({.ramp = GainRamp{kFinalRampDownGainDb, kRampDownOnPauseDuration,
                                       fuchsia::media::audio::RampType::SCALE_LINEAR}});

  // Wait for the ramp to complete.
  const zx::duration delay = kRampDownOnPauseDuration;
  context().threading_model().FidlDomain().PostDelayedTask(std::move(finish_pause_ramp), delay);
}

void AudioRenderer::FinishPauseRamp(std::shared_ptr<PauseRampState> expected_state) {
  TRACE_DURATION("audio", "AudioRenderer::FinishPauseRamp");
  FX_CHECK(expected_state);

  // Skip if the callback was already invoked. This can happen if our pause ramp was
  // interrupted by a call to Play(). We use a shared pointer to avoid ABA problems
  // when the ramp is interrupted by a Play() followed by another Pause().
  if (pause_ramp_state_ != expected_state) {
    return;
  }

  BaseRenderer::PauseInternal([this](int64_t ref_time, int64_t media_time) mutable {
    FX_CHECK(pause_ramp_state_);

    // Restore stream gain.
    PostStreamGainMute({.gain_db = pause_ramp_state_->prior_stream_gain_db});

    // Run all pending callbacks.
    for (auto& f : pause_ramp_state_->callbacks) {
      f(ref_time, media_time);
    }
    for (auto& f : pause_ramp_state_->queued) {
      f();
    }
    pause_ramp_state_ = nullptr;
  });
}

void AudioRenderer::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) {
  TRACE_DURATION("audio", "AudioRenderer::BindGainControl");

  gain_control_bindings_.AddBinding(GainControlBinding::Create(this), std::move(request));
}

fuchsia::media::Usage AudioRenderer::GetStreamUsage() const {
  return fuchsia::media::Usage::WithRenderUsage(fidl::Clone(usage_));
}

constexpr bool kLogUsageVolumeGainActions = true;
constexpr bool kLogSetGainMuteRampCalls = false;
constexpr bool kLogSetGainMuteRampActions = false;

// Set a change to the usage volume+gain_adjustment
void AudioRenderer::RealizeVolume(VolumeCommand volume_command) {
  context().link_matrix().ForEachDestLink(
      *this, [this, volume_command](LinkMatrix::LinkHandle link) {
        FX_CHECK(link.mix_domain) << "Renderer dest link should have a defined mix_domain";
        float gain_db = link.loudness_transform->Evaluate<2>({
            VolumeValue{volume_command.volume},
            GainDbFsValue{volume_command.gain_db_adjustment},
        });

        if constexpr (kLogUsageVolumeGainActions) {
          // TODO(fxbug.dev/51049) Swap this logging for inspect or other real-time gain observation
          FX_LOGS(INFO) << static_cast<const void*>(this) << " (mixer "
                        << static_cast<const void*>(link.mixer.get()) << ") "
                        << StreamUsage::WithRenderUsage(usage_).ToString() << " dest_gain("
                        << (volume_command.ramp.has_value() ? "ramping to " : "") << gain_db
                        << "db) = Vol(" << volume_command.volume << ") + GainAdjustment("
                        << volume_command.gain_db_adjustment << "db)";
        }

        link.mix_domain->PostTask([link, volume_command, gain_db, reporter = &reporter()]() {
          auto& gain = link.mixer->bookkeeping().gain;

          // Stop any in-progress ramping; use this new ramp or gain_db instead
          if (volume_command.ramp.has_value()) {
            gain.SetDestGainWithRamp(gain_db, volume_command.ramp->duration,
                                     volume_command.ramp->ramp_type);
          } else {
            gain.SetDestGain(gain_db);
          }

          reporter->SetFinalGain(link.mixer->bookkeeping().gain.GetGainDb());
        });
      });
}

void AudioRenderer::PostStreamGainMute(StreamGainCommand gain_command) {
  context().link_matrix().ForEachDestLink(
      *this, [this, gain_command](LinkMatrix::LinkHandle link) mutable {
        FX_CHECK(link.mix_domain) << "Renderer dest link should have a defined mix_domain";

        if constexpr (kLogSetGainMuteRampActions) {
          // TODO(fxbug.dev/51049) Swap this logging for inspect or other real-time gain observation
          std::stringstream stream;
          stream << static_cast<const void*>(this) << " (mixer "
                 << static_cast<const void*>(link.mixer.get()) << ") stream (source) Gain: ";
          std::string log_string = stream.str();
          if (gain_command.mute.has_value()) {
            FX_LOGS(INFO) << log_string << "setting mute to "
                          << (gain_command.mute.value() ? "TRUE" : "FALSE");
          }
          if (gain_command.gain_db.has_value()) {
            FX_LOGS(INFO) << log_string << "setting gain to " << gain_command.gain_db.value()
                          << " db";
          }
          if (gain_command.ramp.has_value()) {
            FX_LOGS(INFO) << log_string << "ramping gain to " << gain_command.ramp->end_gain_db
                          << " db, over " << gain_command.ramp->duration.to_usecs() << " usec";
          }
        }

        link.mix_domain->PostTask([link, gain_command, reporter = &reporter()]() mutable {
          auto& gain = link.mixer->bookkeeping().gain;
          if (gain_command.mute.has_value()) {
            gain.SetSourceMute(gain_command.mute.value());
          }
          if (gain_command.gain_db.has_value()) {
            gain.SetSourceGain(gain_command.gain_db.value());
          }
          if (gain_command.ramp.has_value()) {
            gain.SetSourceGainWithRamp(gain_command.ramp->end_gain_db, gain_command.ramp->duration,
                                       gain_command.ramp->ramp_type);
          }

          // Potentially post this as a delayed task instead, if there is a ramp....
          auto final_gain_db = gain.GetGainDb();
          reporter->SetFinalGain(final_gain_db);
        });
      });
}

// Set the stream gain, in each Renderer -> Output audio path. The Gain object contains multiple
// stages. In playback, renderer gain is pre-mix and hence is "source" gain; the usage gain (or
// output gain, if the mixer topology is single-tier) is "dest" gain.
void AudioRenderer::SetGain(float gain_db) {
  SerializeWithPause(std::bind(&AudioRenderer::SetGainInternal, this, gain_db));
}

void AudioRenderer::SetGainInternal(float gain_db) {
  TRACE_DURATION("audio", "AudioRenderer::SetGain");
  if constexpr (kLogSetGainMuteRampCalls) {
    FX_LOGS(INFO) << __FUNCTION__ << "(" << gain_db << " dB)";
  }

  // Before setting stream_gain_db_, always perform this range check.
  if (gain_db > fuchsia::media::audio::MAX_GAIN_DB ||
      gain_db < fuchsia::media::audio::MUTED_GAIN_DB || isnan(gain_db)) {
    FX_LOGS(WARNING) << "SetGain(" << gain_db << " dB) out of range.";
    context().route_graph().RemoveRenderer(*this);
    return;
  }

  PostStreamGainMute({.gain_db = gain_db});

  stream_gain_db_ = gain_db;
  reporter().SetGain(gain_db);
  NotifyGainMuteChanged();
}

// Set a stream gain ramp, in each Renderer -> Output audio path. Renderer gain is pre-mix and
// hence is the Source component in the Gain object.
void AudioRenderer::SetGainWithRamp(float gain_db, int64_t duration_ns,
                                    fuchsia::media::audio::RampType ramp_type) {
  SerializeWithPause(
      std::bind(&AudioRenderer::SetGainWithRampInternal, this, gain_db, duration_ns, ramp_type));
}

void AudioRenderer::SetGainWithRampInternal(float gain_db, int64_t duration_ns,
                                            fuchsia::media::audio::RampType ramp_type) {
  TRACE_DURATION("audio", "AudioRenderer::SetGainWithRamp");
  if constexpr (kLogSetGainMuteRampCalls) {
    FX_LOGS(INFO) << __FUNCTION__ << "(to " << gain_db << " dB over " << duration_ns / 1000
                  << " usec)";
  }

  if (gain_db > fuchsia::media::audio::MAX_GAIN_DB ||
      gain_db < fuchsia::media::audio::MUTED_GAIN_DB || isnan(gain_db)) {
    FX_LOGS(WARNING) << "SetGainWithRamp(" << gain_db << " dB) out of range.";
    context().route_graph().RemoveRenderer(*this);
    return;
  }

  zx::duration duration = zx::nsec(duration_ns);
  PostStreamGainMute({.ramp = GainRamp{gain_db, duration, ramp_type}});

  stream_gain_db_ = gain_db;
  reporter().SetGainWithRamp(gain_db, duration, ramp_type);
  // TODO(mpuryear): implement GainControl notifications for gain ramps.
}

// Set a stream mute, in each Renderer -> Output audio path.
void AudioRenderer::SetMute(bool mute) {
  SerializeWithPause(std::bind(&AudioRenderer::SetMuteInternal, this, mute));
}

void AudioRenderer::SetMuteInternal(bool mute) {
  TRACE_DURATION("audio", "AudioRenderer::SetMute");
  if constexpr (kLogSetGainMuteRampCalls) {
    FX_LOGS(INFO) << __FUNCTION__ << "(" << mute << ")";
  }
  // Only do the work if the request represents a change in state.
  if (mute_ == mute) {
    return;
  }

  PostStreamGainMute({.mute = mute});

  mute_ = mute;
  reporter().SetMute(mute);
  NotifyGainMuteChanged();
}

void AudioRenderer::NotifyGainMuteChanged() {
  TRACE_DURATION("audio", "AudioRenderer::NotifyGainMuteChanged");
  std::lock_guard<std::mutex> lock(mutex_);
  if (notified_gain_db_ == stream_gain_db_ && notified_mute_ == mute_) {
    return;
  }
  notified_gain_db_ = stream_gain_db_;
  notified_mute_ = mute_;

  // TODO(mpuryear): consider whether GainControl events should be disable-able, like MinLeadTime.
  FX_LOGS(DEBUG) << " (" << notified_gain_db_.value() << " dB, mute: " << notified_mute_.value()
                 << ")";

  for (auto& gain_binding : gain_control_bindings_.bindings()) {
    gain_binding->events().OnGainMuteChanged(notified_gain_db_.value(), notified_mute_.value());
  }
}

// Implementation of the GainControl FIDL interface. Just forward to AudioRenderer
void AudioRenderer::GainControlBinding::SetGain(float gain_db) {
  TRACE_DURATION("audio", "AudioRenderer::SetGain");
  owner_->SetGain(gain_db);
}

void AudioRenderer::GainControlBinding::SetGainWithRamp(float gain_db, int64_t duration_ns,
                                                        fuchsia::media::audio::RampType ramp_type) {
  TRACE_DURATION("audio", "AudioRenderer::SetSourceGainWithRamp");
  owner_->SetGainWithRamp(gain_db, duration_ns, ramp_type);
}

void AudioRenderer::GainControlBinding::SetMute(bool mute) {
  TRACE_DURATION("audio", "AudioRenderer::SetMute");
  owner_->SetMute(mute);
}

}  // namespace media::audio
