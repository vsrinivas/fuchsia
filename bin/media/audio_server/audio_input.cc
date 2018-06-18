// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_input.h"

#include "garnet/bin/media/audio_server/audio_device_manager.h"

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

  FXL_LOG(INFO) << "AudioInput Configuring for " << pref_fps << " Hz "
                << pref_chan << " channel(s) sample format(0x" << std::hex
                << static_cast<uint32_t>(pref_fmt) << ")";

  // Send the configuration request, the recompute the distance between our
  // start and end sampling fences.
  driver_->Configure(pref_fps, pref_chan, pref_fmt, kMaxFenceDistance);

  int64_t dist = TimelineRate(pref_fps, ZX_SEC(1)).Scale(kMinFenceDistance);
  FXL_DCHECK(dist < std::numeric_limits<uint32_t>::max());
  driver_->SetEndFenceToStartFenceFrames(static_cast<uint32_t>(dist));
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

}  // namespace audio
}  // namespace media
