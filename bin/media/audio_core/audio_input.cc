// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/audio_input.h"

#include "garnet/bin/media/audio_core/audio_device_manager.h"

namespace media {
namespace audio {

constexpr zx_duration_t kMinFenceDistance = ZX_MSEC(200);
constexpr zx_duration_t kMaxFenceDistance = kMinFenceDistance + ZX_MSEC(20);

// static
fbl::RefPtr<AudioInput> AudioInput::Create(zx::channel channel,
                                           AudioDeviceManager* manager) {
  return fbl::AdoptRef(new AudioInput(std::move(channel), manager));
}

AudioInput::AudioInput(zx::channel channel, AudioDeviceManager* manager)
    : AudioDevice(Type::Input, manager),
      initial_stream_channel_(std::move(channel)) {}

zx_status_t AudioInput::Init() {
  zx_status_t res = AudioDevice::Init();
  if (res != ZX_OK) {
    return res;
  }

  res = driver_->Init(fbl::move(initial_stream_channel_));
  if (res == ZX_OK) {
    state_ = State::Initialized;
  }

  return res;
}

void AudioInput::OnWakeup() {
  // We were poked.  Are we just starting up?
  if (state_ == State::Initialized) {
    if (driver_->GetDriverInfo() != ZX_OK) {
      ShutdownSelf();
    } else {
      state_ = State::FetchingFormats;
    }
    return;
  }

  UpdateDriverGainState();
}

void AudioInput::OnDriverInfoFetched() {
  state_ = State::Idle;

  uint32_t pref_fps = 48000;
  uint32_t pref_chan = 1;
  fuchsia::media::AudioSampleFormat pref_fmt =
      fuchsia::media::AudioSampleFormat::SIGNED_16;

  zx_status_t res = SelectBestFormat(driver_->format_ranges(), &pref_fps,
                                     &pref_chan, &pref_fmt);
  if (res != ZX_OK) {
    FXL_LOG(ERROR)
        << "Audio input failed to find any compatible driver formats.  Req was "
        << pref_fps << " Hz " << pref_chan << " channel(s) sample format(0x"
        << std::hex << static_cast<uint32_t>(pref_fmt) << ")";
    ShutdownSelf();
    return;
  }

  const auto& hw_gain = driver()->hw_gain_state();
  if (hw_gain.min_gain > hw_gain.max_gain) {
    FXL_LOG(ERROR) << "Audio input has invalid gain limits ["
                   << hw_gain.min_gain << ", " << hw_gain.max_gain << "].";
    ShutdownSelf();
    return;
  }

  FXL_LOG(INFO) << "AudioInput Configuring for " << pref_fps << " Hz "
                << pref_chan << " channel(s) sample format(0x" << std::hex
                << static_cast<uint32_t>(pref_fmt) << ")";

  // Send the configuration request, the recompute the distance between our
  // start and end sampling fences.
  driver_->Configure(pref_fps, pref_chan, pref_fmt, kMaxFenceDistance);

  int64_t dist = TimelineRate(pref_fps, ZX_SEC(1)).Scale(kMinFenceDistance);
  FXL_DCHECK(dist < std::numeric_limits<uint32_t>::max());
  driver_->SetEndFenceToStartFenceFrames(static_cast<uint32_t>(dist));

  // Let the AudioDeviceManager know that we are ready to be added to the set of
  // active audio devices.
  ActivateSelf();
}

void AudioInput::OnDriverConfigComplete() {
  driver_->SetPlugDetectEnabled(true);
}

void AudioInput::OnDriverStartComplete() {
  // If we were unplugged while starting, stop now.
  if (!driver_->plugged()) {
    driver_->Stop();
  }
}

void AudioInput::OnDriverStopComplete() {
  // If we were plugged while stopping, start now.
  if (driver_->plugged()) {
    driver_->Start();
  }
}

void AudioInput::OnDriverPlugStateChange(bool plugged, zx_time_t plug_time) {
  if (plugged && (driver_->state() == AudioDriver::State::Configured)) {
    driver_->Start();
  } else if (!plugged && (driver_->state() == AudioDriver::State::Started)) {
    driver_->Stop();
  }

  // Reflect this message to the AudioDeviceManager so it can deal with the
  // routing consequences of the plug state change.
  manager_->ScheduleMainThreadTask([manager = manager_,
                                    output = fbl::WrapRefPtr(this), plugged,
                                    plug_time]() {
    manager->HandlePlugStateChange(output, plugged, plug_time);
  });
}

void AudioInput::ApplyGainLimits(::fuchsia::media::AudioGainInfo* in_out_info,
                                 uint32_t set_flags) {
  // By the time anyone is calling "ApplyGainLimits", we need to have our basic
  // audio gain control capabilities established.
  ZX_DEBUG_ASSERT(driver()->state() != AudioDriver::State::Uninitialized);
  ZX_DEBUG_ASSERT(driver()->state() != AudioDriver::State::MissingDriverInfo);

  const auto& caps = driver()->hw_gain_state();

  // If someone is trying to enable mute, but our hardware does not support
  // enabling mute, clear the flag.
  //
  // TODO(johngro): It should always be possible to mute.  We should maintain a
  // SW flag for implementing mute in case the hardware cannot.
  if (!caps.can_mute) {
    in_out_info->flags &= ~(::fuchsia::media::AudioGainInfoFlag_Mute);
  }

  // Don't allow AGC unless HW supports it.
  if (!caps.can_agc) {
    in_out_info->flags &= ~(::fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  }

  // If the user is attempting to set gain, enforce the gain limits.
  if (set_flags & fuchsia::media::SetAudioGainFlag_GainValid) {
    // This should have been enforced in OnDriverInfoFetched.
    FXL_DCHECK(caps.min_gain <= caps.max_gain);

    // If the hardware has not supplied a valid gain step size, or an
    // ridiculously small step size, just apply a clamp based on min/max.
    constexpr float kStepSizeLimit = 1e-6;
    if (caps.gain_step <= kStepSizeLimit) {
      in_out_info->db_gain =
          fbl::clamp(in_out_info->db_gain, caps.min_gain, caps.max_gain);
    } else {
      int32_t min_steps = static_cast<int32_t>(caps.min_gain / caps.gain_step);
      int32_t max_steps = static_cast<int32_t>(caps.max_gain / caps.gain_step);
      int32_t steps = fbl::clamp(
          static_cast<int32_t>(in_out_info->db_gain / caps.gain_step),
          min_steps, max_steps);
      in_out_info->db_gain = static_cast<float>(steps) * caps.gain_step;
    }
  }
}

void AudioInput::UpdateDriverGainState() {
  if ((state_ != State::Idle) || (device_settings_ == nullptr)) {
    return;
  }

  AudioDeviceSettings::GainState state;
  audio_set_gain_flags_t dirty_flags =
      device_settings_->SnapshotGainState(&state);
  if (!dirty_flags) {
    return;
  }

  driver()->SendSetGain(state, dirty_flags);
}

}  // namespace audio
}  // namespace media
