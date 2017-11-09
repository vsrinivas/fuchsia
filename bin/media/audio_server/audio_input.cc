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

MediaResult AudioInput::Init() {
  MediaResult init_res = AudioDevice::Init();
  if (init_res != MediaResult::OK) {
    return init_res;
  }

  if (driver_->Init(fbl::move(initial_stream_channel_)) != ZX_OK) {
    return MediaResult::INTERNAL_ERROR;
  }

  state_ = State::Initialized;
  return MediaResult::OK;
}

void AudioInput::OnWakeup() {
  // We were poked.  Are we just starting up?
  if (state_ == State::Initialized) {
    if (driver_->GetSupportedFormats() != ZX_OK) {
      ShutdownSelf();
    } else {
      state_ = State::FetchingFormats;
    }
    return;
  }
}

void AudioInput::OnDriverGetFormatsComplete() {
  state_ = State::Idle;

  // TODO(johngro) : select the best valid driver mode, do not hardcode this.
  driver_->Configure(48000, 2, AudioSampleFormat::SIGNED_16, kMaxFenceDistance);
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
  // clang-format off
  manager_->ScheduleMessageLoopTask(
    [ manager = manager_,
      output = fbl::WrapRefPtr(this),
      plugged,
      plug_time ]() {
      manager->HandlePlugStateChange(output, plugged, plug_time);
    });
  // clang-format on
}

}  // namespace audio
}  // namespace media
