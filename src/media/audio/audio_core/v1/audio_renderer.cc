// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/audio_renderer.h"

#include <fuchsia/media/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/media/audio/audio_core/v1/audio_admin.h"
#include "src/media/audio/audio_core/v1/logging_flags.h"
#include "src/media/audio/audio_core/v1/reporter.h"
#include "src/media/audio/audio_core/v1/stream_usage.h"
#include "src/media/audio/audio_core/v1/stream_volume_manager.h"
#include "src/media/audio/lib/clock/utils.h"

namespace media::audio {

namespace {

// Constants used when using dropout checks
constexpr bool kEnableDropoutChecks = false;
constexpr bool kDisplayPacketOnDropout = false;

// Dropout checkers are currently limited to float32 data only.
constexpr fuchsia::media::AudioSampleFormat kDropoutChecksFormat =
    fuchsia::media::AudioSampleFormat::FLOAT;
// Only enable the dropout checks if the renderer also fits these other dimensions.
constexpr int32_t kDropoutChecksChannelCount = 2;
constexpr int32_t kDropoutChecksFrameRate = 44100;

// Const values used by PowerChecker to analyze the RMS power of incoming packets.
// Adjust the window size and min RMS level as needed, for the test content being used.
// Set channel count, frame rate and sample_format so that only the client of interest is analyzed.
// Current values were used for a stereo float 44.1k source stream containing ampl-0.5 white noise.
constexpr int64_t kRmsWindowInFrames = 512;
constexpr double kRmsLevelMin = 0.065;  // 0.16;

// With the controlled content (sine|const|ramp|noise at full-scale amplitude) that is
// commonly used with this dropout checker, consecutive silent frames should not occur.
constexpr int64_t kConsecutiveSilenceFramesAllowed = 1;

}  // namespace

AudioRenderer::AudioRenderer(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request, Context* context)
    : BaseRenderer(std::move(audio_renderer_request), context),
      mix_profile_period_(context->process_config().mix_profile_config().period) {
  context->volume_manager().AddStream(this);
  reporter().SetUsage(RenderUsageFromFidlRenderUsage(usage_));

  if constexpr (kLogRendererCtorDtorCalls) {
    FX_LOGS(INFO) << __FUNCTION__ << " (" << this << ") *****";
  }
}

AudioRenderer::~AudioRenderer() {
  if constexpr (kLogRendererCtorDtorCalls) {
    FX_LOGS(INFO) << __FUNCTION__ << " (" << this
                  << ") usage:" << RenderUsageToString(RenderUsageFromFidlRenderUsage(usage_))
                  << " *****";
  }

  // We (not ~BaseRenderer) must call this, because our ReportStop is gone when parent dtor runs
  ReportStopIfStarted();

  context().volume_manager().RemoveStream(this);
}

void AudioRenderer::OnLinkAdded() {
  // With a link, our Mixer and Gain objects have been created, so we can set initial gain levels.
  if (mute_ || stream_gain_db_ != 0.0f) {
    if constexpr (kLogRendererSetGainMuteRampCalls) {
      FX_LOGS(INFO) << static_cast<const void*>(this)
                    << " SetInitialGainMute gain=" << stream_gain_db_ << "dB, mute=" << mute_;
    }
    PostStreamGainMute({
        .gain_db = stream_gain_db_,
        .mute = mute_,
    });
  }
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

  if constexpr (kLogAudioRendererSetUsageCalls) {
    FX_LOGS(INFO) << __FUNCTION__ << " (" << this << ") changed from "
                  << RenderUsageToString(RenderUsageFromFidlRenderUsage(usage_)) << " to "
                  << RenderUsageToString(RenderUsageFromFidlRenderUsage(usage)) << " *****";
  }

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

  // Only create a PowerChecker if enabled, and if the renderer fits our specifications
  if constexpr (kEnableDropoutChecks) {
    if (format_->sample_format() == kDropoutChecksFormat &&
        format_->frames_per_second() == kDropoutChecksFrameRate &&
        format_->channels() == kDropoutChecksChannelCount) {
      std::ostringstream out;
      out << "AudioRenderer(" << this << ")";
      power_checker_ = std::make_unique<PowerChecker>(kRmsWindowInFrames, format_->channels(),
                                                      kRmsLevelMin, out.str());
      silence_checker_ = std::make_unique<SilenceChecker>(kConsecutiveSilenceFramesAllowed,
                                                          format_->channels(), out.str());
    }
  }

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

// Analyze a packet for dropouts; return true if no dropouts. Because it is called only when
// debugging specific conditions/content, this function assumes that the packet format is FLOAT.
bool AudioRenderer::AnalyzePacket(fuchsia::media::StreamPacket packet) {
  auto payload_buffer = payload_buffers().find(packet.payload_buffer_id)->second;
  auto packet_data = reinterpret_cast<const float*>(
      reinterpret_cast<uint8_t*>(payload_buffer->start()) + packet.payload_offset);
  int64_t frame_count = packet.payload_size / format()->bytes_per_frame();
  auto frame_start = frames_received();
  auto rms_check =
      power_checker_ ? power_checker_->Check(packet_data, frame_start, frame_count, true) : true;
  auto silence_check = silence_checker_
                           ? silence_checker_->Check(packet_data, frame_start, frame_count, true)
                           : true;
  // If packet fails either check, display its metadata. Limit logging to avoid log storms.
  if (!rms_check || !silence_check) {
    FX_LOGS_FIRST_N(INFO, 200) << "********** Dropout detected (rms_check "
                               << (rms_check ? "pass" : "FAIL") << ", consec_silence_check "
                               << (silence_check ? "pass" : "FAIL")
                               << ") in packet payload_buffer_id " << packet.payload_buffer_id
                               << ", offset " << packet.payload_offset << " (bytes), size "
                               << packet.payload_size << " (bytes), frames 0 to " << frame_count - 1
                               << ",  pts "
                               << (packet.pts == fuchsia::media::NO_TIMESTAMP
                                       ? "NO_TIMESTAMP"
                                       : std::to_string(packet.pts).c_str())
                               << " **********";
    // If the debug flag is enabled, also display the packet's entire set of data values.
    if constexpr (kDisplayPacketOnDropout) {
      std::ostringstream out;
      for (auto i = 0; i < frame_count; ++i) {
        out << "  [" << std::setw(3) << i << "]";
        auto sample_index = i * format()->channels();
        for (auto chan = 0; chan < format()->channels(); ++chan) {
          out << std::setprecision(6) << std::fixed << std::setw(10)
              << packet_data[sample_index + chan];
        }
        if ((i + 1) % (8 / format()->channels()) == 0 || (i + 1) == frame_count) {
          // To limit this to approx. the same interval as the logging above, we assume 10-msec
          // packets at the specified stereo-44.1k format.
          FX_LOGS_FIRST_N(INFO, 22050) << out.str();
          out.str(std::string());  // clear the stream's contents to prep for for the next line
        }
      }
    }
  }
  return rms_check && silence_check;
}

void AudioRenderer::SendPacketInternal(fuchsia::media::StreamPacket packet,
                                       SendPacketCallback callback) {
  SerializeWithPause([this, packet, callback = std::move(callback)]() mutable {
    BaseRenderer::SendPacketInternal(packet, std::move(callback));

    if constexpr (kEnableDropoutChecks) {
      if (power_checker_ || silence_checker_) {
        AnalyzePacket(packet);
      }
    }
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
  if constexpr (kLogRendererPlayCalls) {
    FX_LOGS(INFO) << "Renderer(" << this << ") Play(ref time "
                  << (reference_time.get() == fuchsia::media::NO_TIMESTAMP
                          ? "NO_TIMESTAMP"
                          : std::to_string(reference_time.get()))
                  << ", media time  "
                  << (media_time.get() == fuchsia::media::NO_TIMESTAMP
                          ? "NO_TIMESTAMP"
                          : std::to_string(media_time.get()))
                  << ")";
  }

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
    PostStreamGainMute({
        .gain_db = kInitialRampUpGainDb,
        .ramp = GainRamp{.end_gain_db = 0.0f, .duration = kRampUpOnPlayDuration},
        .control = StreamGainCommand::Control::ADJUSTMENT,
    });
  }

  BaseRenderer::PlayInternal(reference_time, media_time, std::move(callback));
}

void AudioRenderer::PauseInternal(PauseCallback callback) {
  if constexpr (kLogRendererPauseCalls) {
    FX_LOGS(INFO) << "Renderer(" << this << ") Pause";
  }

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
  if (callback) {
    pause_ramp_state_->callbacks.push_back(std::move(callback));
  }

  // Callback to tear down pause_ramp_state_ when the ramp completes.
  // We add a shared self-reference in case the renderer is unbound before this callback runs.
  auto finish_pause_ramp = [this, self = shared_from_this(), state = pause_ramp_state_]() mutable {
    FinishPauseRamp(state);
  };

  // Don't call SetGainInternal/SetGainWithRampInternal to avoid gain notifications.
  PostStreamGainMute({
      .ramp = GainRamp{.end_gain_db = kFinalRampDownGainDb, .duration = kRampDownOnPauseDuration},
      .control = StreamGainCommand::Control::ADJUSTMENT,
  });

  // Before restoring the original gain, wait for a mix to reflect the rampdown. Gain is calculated
  // at the start of each mix. Unless we wait for the next one, our SetGain cancels any ongoing
  // rampdowns, leading to the discontinuities these ramp-downs intend to eliminate.
  const zx::duration delay = mix_profile_period_ + kRampDownOnPauseDuration;
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

// Set a change to the usage volume+gain_adjustment
void AudioRenderer::RealizeVolume(VolumeCommand volume_command) {
  context().link_matrix().ForEachDestLink(
      *this, [this, volume_command](LinkMatrix::LinkHandle link) {
        FX_CHECK(link.mix_domain) << "Renderer dest link should have a defined mix_domain";
        float gain_db = link.loudness_transform->Evaluate<2>({
            VolumeValue{volume_command.volume},
            GainDbFsValue{volume_command.gain_db_adjustment},
        });

        if constexpr (kLogRenderUsageVolumeGainActions) {
          // TODO(fxbug.dev/51049) Swap this logging for inspect or other real-time gain observation
          FX_LOGS(INFO) << static_cast<const void*>(this) << " (gain " << &(link.mixer->gain)
                        << ", mixer " << static_cast<const void*>(link.mixer.get()) << ") "
                        << StreamUsage::WithRenderUsage(usage_).ToString() << " dest_gain("
                        << (volume_command.ramp.has_value() ? "ramping to " : "") << gain_db
                        << "db) = Vol(" << volume_command.volume << ") + GainAdjustment("
                        << volume_command.gain_db_adjustment << "db)";
        }

        link.mix_domain->PostTask([link, volume_command, gain_db, reporter = &reporter()]() {
          auto& gain = link.mixer->gain;

          // Stop any in-progress ramping; use this new ramp or gain_db instead
          if (volume_command.ramp.has_value()) {
            gain.SetDestGainWithRamp(gain_db, volume_command.ramp->duration,
                                     volume_command.ramp->ramp_type);
          } else {
            gain.SetDestGain(gain_db);
          }

          reporter->SetFinalGain(link.mixer->gain.GetUnadjustedGainDb());
        });
      });
}

void AudioRenderer::PostStreamGainMute(StreamGainCommand gain_command) {
  context().link_matrix().ForEachDestLink(*this, [this, gain_command](
                                                     LinkMatrix::LinkHandle link) mutable {
    FX_CHECK(link.mix_domain) << "Renderer dest link should have a defined mix_domain";

    if constexpr (kLogRendererSetGainMuteRampActions) {
      // TODO(fxbug.dev/51049) Swap this logging for inspect or other real-time gain observation
      std::stringstream stream;
      stream << static_cast<const void*>(this) << " (gain " << &(link.mixer->gain) << ", mixer "
             << static_cast<const void*>(link.mixer.get()) << ") stream ("
             << (gain_command.control == StreamGainCommand::Control::ADJUSTMENT ? "adjustment"
                                                                                : "source")
             << ") Gain: ";
      std::string log_string = stream.str();
      if (gain_command.mute.has_value()) {
        FX_CHECK(gain_command.control == StreamGainCommand::Control::SOURCE);
        FX_LOGS(INFO) << log_string << "setting mute to "
                      << (gain_command.mute.value() ? "TRUE" : "FALSE");
      }
      if (gain_command.gain_db.has_value()) {
        FX_LOGS(INFO) << log_string << "setting gain to " << gain_command.gain_db.value() << " db";
      }
      if (gain_command.ramp.has_value()) {
        FX_LOGS(INFO) << log_string << "ramping gain to " << gain_command.ramp->end_gain_db
                      << " db, over " << gain_command.ramp->duration.to_usecs() << " usec";
      }
    }

    link.mix_domain->PostTask([link, gain_command, reporter = &reporter()]() mutable {
      auto& gain = link.mixer->gain;
      switch (gain_command.control) {
        case StreamGainCommand::Control::ADJUSTMENT:
          if (gain_command.gain_db.has_value()) {
            gain.SetGainAdjustment(gain_command.gain_db.value());
          }
          if (gain_command.ramp.has_value()) {
            gain.SetGainAdjustmentWithRamp(gain_command.ramp->end_gain_db,
                                           gain_command.ramp->duration,
                                           gain_command.ramp->ramp_type);
          }
          break;
        case StreamGainCommand::Control::SOURCE:
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
          break;
      }

      // Potentially post this as a delayed task instead, if there is a ramp.
      auto final_unadjusted_gain_db = gain.GetUnadjustedGainDb();
      reporter->SetFinalGain(final_unadjusted_gain_db);
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
  if constexpr (kLogRendererSetGainMuteRampCalls) {
    FX_LOGS(INFO) << static_cast<const void*>(this) << " " << __FUNCTION__ << "(" << gain_db
                  << " dB)";
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
  if constexpr (kLogRendererSetGainMuteRampCalls) {
    FX_LOGS(INFO) << static_cast<const void*>(this) << " " << __FUNCTION__ << "(to " << gain_db
                  << " dB over " << duration_ns / 1000 << " usec)";
  }

  if (duration_ns <= 0) {
    FX_LOGS(WARNING) << "SetGainWithRamp ramp duration (" << duration_ns
                     << " nsec) is non-positive; calling SetGain(" << gain_db << ") instead.";
    SetGainInternal(gain_db);
    return;
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
  if constexpr (kLogRendererSetGainMuteRampCalls) {
    FX_LOGS(INFO) << static_cast<const void*>(this) << " " << __FUNCTION__ << "(" << mute << ")";
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
