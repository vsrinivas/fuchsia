// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_capturer.h"

#include <lib/fit/defer.h>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/audio_core/stream_usage.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

constexpr float kInitialCaptureGainDb = Gain::kUnityGainDb;

}

AudioCapturer::AudioCapturer(fuchsia::media::AudioCapturerConfiguration configuration,
                             std::optional<Format> format,
                             fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request,
                             Context* context)
    : BaseCapturer(std::move(format), std::move(request), context),
      loopback_(configuration.is_loopback()),
      mute_(false),
      stream_gain_db_(kInitialCaptureGainDb) {
  FX_DCHECK(context);
  if (!loopback_) {
    context->volume_manager().AddStream(this);
    if (configuration.input().has_usage()) {
      usage_ = configuration.input().usage();
    }
  }
  reporter().SetUsage(CaptureUsageFromFidlCaptureUsage(usage_));
}

AudioCapturer::~AudioCapturer() {
  if (!loopback_) {
    context().volume_manager().RemoveStream(this);
  }
}

void AudioCapturer::ReportStart() {
  BaseCapturer::ReportStart();
  if (!loopback_) {
    context().audio_admin().UpdateCapturerState(usage_, true, this);
  }
}

void AudioCapturer::ReportStop() {
  BaseCapturer::ReportStop();
  if (!loopback_) {
    context().audio_admin().UpdateCapturerState(usage_, false, this);
  }
}

void AudioCapturer::OnStateChanged(State old_state, State new_state) {
  BaseCapturer::OnStateChanged(old_state, new_state);
  if (!loopback_ && new_state == State::OperatingSync) {
    context().volume_manager().NotifyStreamChanged(this);
  }
}

void AudioCapturer::SetRoutingProfile(bool routable) {
  auto profile =
      RoutingProfile{.routable = routable, .usage = StreamUsage::WithCaptureUsage(capture_usage())};
  context().route_graph().SetCapturerRoutingProfile(*this, std::move(profile));

  // Once we route the capturer, we accept the default reference clock if one hasn't yet been set.
  if (routable) {
    reference_clock_is_set_ = true;
  }
}

void AudioCapturer::OnLinkAdded() {
  BaseCapturer::OnLinkAdded();
  if (!loopback_) {
    context().volume_manager().NotifyStreamChanged(this);
  }
}

constexpr auto kRequiredClockRights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ;
// If received clock is null, use optimal clock. Otherwise, use this new clock. Fail and disconnect,
// if the client-submitted clock has insufficient rights (and strip off other rights such as WRITE).
void AudioCapturer::SetReferenceClock(zx::clock ref_clock) {
  TRACE_DURATION("audio", "AudioCapturer::SetReferenceClock");
  AUDIO_LOG_OBJ(DEBUG, this);

  auto cleanup = fit::defer([this]() { BeginShutdown(); });

  // We cannot change the reference clock, once set. Also, once the capturer is routed to a device
  // (which occurs upon AddPayloadBuffer), we set the default clock if one has not yet been set.
  if (reference_clock_is_set_) {
    FX_LOGS(WARNING) << "Cannot change reference clock once it is set!";
    return;
  }

  zx_status_t status;
  if (ref_clock.is_valid()) {
    // If ref_clock doesn't have DUPLICATE or READ or TRANSFER rights, return (i.e. shutdown).
    status = ref_clock.replace(kRequiredClockRights, &ref_clock);
    if (status != ZX_OK) {
      FX_PLOGS(WARNING, status) << "Could not set rights on client-submitted reference clock";
      return;
    }
    SetClock(AudioClock::CreateAsCustom(std::move(ref_clock)));
  } else {
    // Optimal clock is writable: to achieve "no-SRC", we fine-tune it to match the device clock.
    // TODO(mpuryear): Client may rate-adjust the clock at any time; we should only use SincSampler
    SetOptimalReferenceClock();
  }

  reference_clock_is_set_ = true;

  cleanup.cancel();
}

void AudioCapturer::SetPcmStreamType(fuchsia::media::AudioStreamType stream_type) {
  TRACE_DURATION("audio", "AudioCapturer::SetPcmStreamType");
  // If something goes wrong, hang up the phone and shutdown.
  auto cleanup = fit::defer([this]() { BeginShutdown(); });

  // If our shared buffer has been assigned, we are operating and our mode can no longer be changed.
  State state = capture_state();
  if (state != State::WaitingForVmo) {
    FX_LOGS(WARNING) << "Cannot change format after payload buffer has been added"
                     << "(state = " << static_cast<uint32_t>(state) << ")";
    return;
  }

  auto format_result = Format::Create(stream_type);
  if (format_result.is_error()) {
    FX_LOGS(WARNING) << "AudioCapturer: PcmStreamType is invalid";
    return;
  }

  reporter().SetStreamType(stream_type);

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
  if (loopback_) {
    FX_LOGS(WARNING) << "SetUsage on loopback capturer is not allowed";
    return;
  }

  ReportStop();
  reporter().SetUsage(CaptureUsageFromFidlCaptureUsage(usage));
  usage_ = usage;
  context().volume_manager().NotifyStreamChanged(this);
  State state = capture_state();
  SetRoutingProfile(StateIsRoutable(state));
  if (state == State::OperatingAsync) {
    ReportStart();
  }
  if (state == State::OperatingSync) {
    if (has_pending_packets()) {
      ReportStart();
    }
  }
}

bool AudioCapturer::GetStreamMute() const { return mute_; }

fuchsia::media::Usage AudioCapturer::GetStreamUsage() const {
  // We should only be calling these from the StreamVolumeManager. We don't register LOOPBACK
  // capturers with the StreamVolumeManager since those capturers do not have a compatible
  // usage.
  FX_DCHECK(!loopback_);
  fuchsia::media::Usage usage;
  usage.set_capture_usage(usage_);
  return usage;
}

void AudioCapturer::RealizeVolume(VolumeCommand volume_command) {
  if (volume_command.ramp.has_value()) {
    FX_LOGS(WARNING)
        << "Capturer gain ramping is not implemented";
  }

  context().link_matrix().ForEachSourceLink(
      *this, [this, volume_command](LinkMatrix::LinkHandle link) {
        float gain_db = link.loudness_transform->Evaluate<3>({
            VolumeValue{volume_command.volume},
            GainDbFsValue{volume_command.gain_db_adjustment},
            GainDbFsValue{stream_gain_db_.load()},
        });
        // TODO(fxbug.dev/51049) Logging should be removed upon creation of inspect tool or other
        // real-time method for gain observation
        if (gain_db != 0.0) {
          FX_LOGS(INFO) << this << " " << StreamUsage::WithCaptureUsage(usage_).ToString()
                        << " Gain(" << gain_db << "db) = "
                        << "Vol(" << volume_command.volume << ") + GainAdjustment("
                        << volume_command.gain_db_adjustment << "db) + StreamGain("
                        << stream_gain_db_ << "db)";
        }

        mix_domain().PostTask(
            [link, gain_db]() { link.mixer->bookkeeping().gain.SetDestGain(gain_db); });
      });
}

void AudioCapturer::SetGain(float gain_db) {
  TRACE_DURATION("audio", "AudioCapturer::SetGain");
  // Before setting stream_gain_db_, we should always perform this range check.
  if ((gain_db < fuchsia::media::audio::MUTED_GAIN_DB) ||
      (gain_db > fuchsia::media::audio::MAX_GAIN_DB) || isnan(gain_db)) {
    FX_LOGS(WARNING) << "SetGain(" << gain_db << " dB) out of range.";
    BeginShutdown();
    return;
  }

  // If the incoming SetGain request represents no change, we're done
  // (once we add gain ramping, this type of check isn't workable).
  if (stream_gain_db_ == gain_db) {
    return;
  }

  reporter().SetGain(gain_db);

  stream_gain_db_.store(gain_db);
  if (!loopback_) {
    context().volume_manager().NotifyStreamChanged(this);
  }

  NotifyGainMuteChanged();
}

void AudioCapturer::SetMute(bool mute) {
  TRACE_DURATION("audio", "AudioCapturer::SetMute");
  // If the incoming SetMute request represents no change, we're done.
  if (mute_ == mute) {
    return;
  }

  reporter().SetMute(mute);

  mute_ = mute;

  if (!loopback_) {
    context().volume_manager().NotifyStreamChanged(this);
  }
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
