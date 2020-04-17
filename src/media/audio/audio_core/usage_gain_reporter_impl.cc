// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/usage_gain_reporter_impl.h"

namespace media::audio {

fidl::InterfaceRequestHandler<fuchsia::media::UsageGainReporter>
UsageGainReporterImpl::GetFidlRequestHandler() {
  return bindings_.GetHandler(this);
}

void UsageGainReporterImpl::RegisterListener(
    std::string device_unique_id, fuchsia::media::Usage usage,
    fidl::InterfaceHandle<fuchsia::media::UsageGainListener> usage_gain_listener_handler) {
  auto usage_gain_listener = usage_gain_listener_handler.Bind();

  // TODO(50077): Verify the device exists and grab its output device
  // profile.
  const auto loudness_transform = process_config_.default_loudness_transform();
  auto listener = std::make_unique<Listener>(loudness_transform, std::move(usage),
                                             std::move(usage_gain_listener));
  stream_volume_manager_.AddStream(listener.get());

  listeners_.insert(std::move(listener));
}

void UsageGainReporterImpl::Listener::RealizeVolume(VolumeCommand volume_command) {
  const auto gain_db = loudness_transform_->Evaluate<2>(
      {VolumeValue{volume_command.volume}, GainDbFsValue{volume_command.gain_db_adjustment}});

  unacked_messages_++;
  usage_gain_listener_->OnGainMuteChanged(/*muted=*/false, gain_db,
                                          [this]() { unacked_messages_--; });
}

}  // namespace media::audio
