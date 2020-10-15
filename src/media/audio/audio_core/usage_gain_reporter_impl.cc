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
  const auto deserialize_result = AudioDevice::UniqueIdFromString(device_unique_id);
  if (deserialize_result.is_error()) {
    FX_LOGS(WARNING) << "UsageGainReporter client provided invalid device id";
    return;
  }
  const auto& deserialized_id = deserialize_result.value();

  auto devices = device_registry_.GetDeviceInfos();
  const auto it = std::find_if(devices.begin(), devices.end(), [&device_unique_id](auto candidate) {
    return candidate.unique_id == device_unique_id;
  });
  if (it == devices.end()) {
    FX_LOGS(WARNING) << "UsageGainReporter client cannot listen: device id not found";
    return;
  }

  auto usage_gain_listener = usage_gain_listener_handler.Bind();

  const auto& device_config = process_config_.device_config();
  const auto& output_device_profile = device_config.output_device_profile(deserialized_id);
  auto listener = std::make_unique<Listener>(output_device_profile, std::move(usage),
                                             std::move(usage_gain_listener));
  stream_volume_manager_.AddStream(listener.get());

  listeners_.insert(std::move(listener));
}

void UsageGainReporterImpl::Listener::RealizeVolume(VolumeCommand volume_command) {
  if (!independent_volume_control_) {
    const auto gain_db = loudness_transform_->Evaluate<2>(
        {VolumeValue{volume_command.volume}, GainDbFsValue{volume_command.gain_db_adjustment}});

    unacked_messages_++;
    usage_gain_listener_->OnGainMuteChanged(/*muted=*/false, gain_db,
                                            [this]() { unacked_messages_--; });
  }
}

}  // namespace media::audio
