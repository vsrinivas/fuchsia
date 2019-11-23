// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_input.h"

#include <algorithm>

#include <trace/event.h>

#include "src/media/audio/audio_core/audio_driver.h"

namespace media::audio {

constexpr zx::duration kMinFenceDistance = zx::msec(200);
constexpr zx::duration kMaxFenceDistance = kMinFenceDistance + zx::msec(20);

// static
fbl::RefPtr<AudioInput> AudioInput::Create(zx::channel channel, ThreadingModel* threading_model,
                                           DeviceRegistry* registry) {
  return fbl::AdoptRef(new AudioInput(std::move(channel), threading_model, registry));
}

AudioInput::AudioInput(zx::channel channel, ThreadingModel* threading_model,
                       DeviceRegistry* registry)
    : AudioDevice(Type::Input, threading_model, registry),
      initial_stream_channel_(std::move(channel)) {}

zx_status_t AudioInput::Init() {
  TRACE_DURATION("audio", "AudioInput::Init");
  zx_status_t res = AudioDevice::Init();
  if (res != ZX_OK) {
    return res;
  }

  res = driver()->Init(std::move(initial_stream_channel_));
  if (res == ZX_OK) {
    state_ = State::Initialized;
  }

  return res;
}

void AudioInput::OnWakeup() {
  TRACE_DURATION("audio", "AudioInput::OnWakeup");
  // We were poked.  Are we just starting up?
  if (state_ == State::Initialized) {
    if (driver()->GetDriverInfo() != ZX_OK) {
      ShutdownSelf();
    } else {
      state_ = State::FetchingFormats;
    }
    return;
  }

  UpdateDriverGainState();
}

void AudioInput::OnDriverInfoFetched() {
  TRACE_DURATION("audio", "AudioInput::OnDriverInfoFetched");
  state_ = State::Idle;

  uint32_t pref_fps = 48000;
  uint32_t pref_chan = 1;
  fuchsia::media::AudioSampleFormat pref_fmt = fuchsia::media::AudioSampleFormat::SIGNED_16;

  zx_status_t res = SelectBestFormat(driver()->format_ranges(), &pref_fps, &pref_chan, &pref_fmt);
  if (res != ZX_OK) {
    FX_LOGS(ERROR) << "Audio input failed to find any compatible driver formats.  Req was "
                   << pref_fps << " Hz " << pref_chan << " channel(s) sample format(0x" << std::hex
                   << static_cast<uint32_t>(pref_fmt) << ")";
    ShutdownSelf();
    return;
  }
  Format selected_format = Format(fuchsia::media::AudioStreamType{
      .sample_format = pref_fmt,
      .channels = pref_chan,
      .frames_per_second = pref_fps,
  });

  const auto& hw_gain = driver()->hw_gain_state();
  if (hw_gain.min_gain > hw_gain.max_gain) {
    FX_LOGS(ERROR) << "Audio input has invalid gain limits [" << hw_gain.min_gain << ", "
                   << hw_gain.max_gain << "].";
    ShutdownSelf();
    return;
  }

  // TODO(mpuryear): Save to the hub the configured format for this input.

  // Send config request; recompute distance between start|end sampling fences.
  driver()->Configure(selected_format, kMaxFenceDistance);

  int64_t dist = TimelineRate(pref_fps, ZX_SEC(1)).Scale(kMinFenceDistance.to_nsecs());
  FX_DCHECK(dist < std::numeric_limits<uint32_t>::max());
  driver()->SetEndFenceToStartFenceFrames(static_cast<uint32_t>(dist));

  // Tell AudioDeviceManager it can add us to the set of active audio devices.
  ActivateSelf();
}

void AudioInput::OnDriverConfigComplete() {
  TRACE_DURATION("audio", "AudioInput::OnDriverConfigComplete");
  driver()->SetPlugDetectEnabled(true);
}

void AudioInput::OnDriverStartComplete() {
  TRACE_DURATION("audio", "AudioInput::OnDriverStartComplete");
  // If we were unplugged while starting, stop now.
  if (!driver()->plugged()) {
    driver()->Stop();
  }
}

void AudioInput::OnDriverStopComplete() {
  TRACE_DURATION("audio", "AudioInput::OnDriverStopComplete");
  // If we were plugged while stopping, start now.
  if (driver()->plugged()) {
    driver()->Start();
  }
}

void AudioInput::OnDriverPlugStateChange(bool plugged, zx::time plug_time) {
  TRACE_DURATION("audio", "AudioInput::OnDriverPlugStateChange");
  if (plugged && (driver()->state() == AudioDriver::State::Configured)) {
    driver()->Start();
  } else if (!plugged && (driver()->state() == AudioDriver::State::Started)) {
    driver()->Stop();
  }

  // Reflect this message to the AudioDeviceManager so it can deal with the
  // routing consequences of the plug state change.
  threading_model().FidlDomain().PostTask([output = fbl::RefPtr(this), plugged, plug_time]() {
    output->device_registry().OnPlugStateChanged(std::move(output), plugged, plug_time);
  });
}

void AudioInput::ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info, uint32_t set_flags) {
  TRACE_DURATION("audio", "AudioInput::ApplyGainLimits");
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
    in_out_info->flags &= ~(fuchsia::media::AudioGainInfoFlag_Mute);
  }

  // Don't allow AGC unless HW supports it.
  if (!caps.can_agc) {
    in_out_info->flags &= ~(fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  }

  // If the user is attempting to set gain, enforce the gain limits.
  if (set_flags & fuchsia::media::SetAudioGainFlag_GainValid) {
    // This should have been enforced in OnDriverInfoFetched.
    FX_DCHECK(caps.min_gain <= caps.max_gain);

    // If the hardware has not supplied a valid gain step size, or an
    // ridiculously small step size, just apply a clamp based on min/max.
    constexpr float kStepSizeLimit = 1e-6;
    if (caps.gain_step <= kStepSizeLimit) {
      in_out_info->gain_db = std::clamp(in_out_info->gain_db, caps.min_gain, caps.max_gain);
    } else {
      auto min_steps = static_cast<int32_t>(caps.min_gain / caps.gain_step);
      auto max_steps = static_cast<int32_t>(caps.max_gain / caps.gain_step);
      int32_t steps = std::clamp(static_cast<int32_t>(in_out_info->gain_db / caps.gain_step),
                                 min_steps, max_steps);
      in_out_info->gain_db = static_cast<float>(steps) * caps.gain_step;
    }
  }
}

void AudioInput::UpdateDriverGainState() {
  TRACE_DURATION("audio", "AudioInput::UpdateDriverGainState");
  auto settings = device_settings();
  if ((state_ != State::Idle) || (settings == nullptr)) {
    return;
  }

  AudioDeviceSettings::GainState state;
  audio_set_gain_flags_t dirty_flags = settings->SnapshotGainState(&state);
  if (!dirty_flags) {
    return;
  }

  driver()->SendSetGain(state, dirty_flags);
}

}  // namespace media::audio
