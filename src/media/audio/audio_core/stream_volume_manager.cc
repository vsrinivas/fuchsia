// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/stream_volume_manager.h"

namespace media::audio {

const UsageGainSettings& StreamVolumeManager::GetUsageGainSettings() const {
  return usage_gain_settings_;
}

void StreamVolumeManager::SetUsageGain(fuchsia::media::Usage usage, float gain_db) {
  usage_gain_settings_.SetUsageGain(fidl::Clone(usage), gain_db);
  UpdateStreamsWithUsage(std::move(usage));
}

void StreamVolumeManager::SetUsageGainAdjustment(fuchsia::media::Usage usage, float gain_db) {
  usage_gain_settings_.SetUsageGainAdjustment(fidl::Clone(usage), gain_db);
  UpdateStreamsWithUsage(std::move(usage));
}

void StreamVolumeManager::NotifyStreamChanged(StreamVolume* stream_volume) {
  UpdateStream(stream_volume, std::nullopt);
}

void StreamVolumeManager::NotifyStreamChanged(StreamVolume* stream_volume, Ramp ramp) {
  UpdateStream(stream_volume, ramp);
}

void StreamVolumeManager::AddStream(StreamVolume* stream_volume) {
  stream_volumes_.insert(stream_volume);
}
void StreamVolumeManager::RemoveStream(StreamVolume* stream_volume) {
  stream_volumes_.erase(stream_volume);
}

void StreamVolumeManager::UpdateStreamsWithUsage(fuchsia::media::Usage usage) {
  for (auto& stream : stream_volumes_) {
    if (fidl::Equals(stream->GetStreamUsage(), usage)) {
      UpdateStream(stream, std::nullopt);
    }
  }
}

void StreamVolumeManager::UpdateStream(StreamVolume* stream, std::optional<Ramp> ramp) {
  const auto usage = stream->GetStreamUsage();
  const auto usage_gain = usage_gain_settings_.GetUsageGain(fidl::Clone(usage));
  const auto usage_volume = usage_volume_settings_.GetUsageVolume(std::move(usage));

  const auto gain_adjustment =
      stream->GetStreamMute() ? fuchsia::media::audio::MUTED_GAIN_DB : usage_gain;

  stream->RealizeVolume(VolumeCommand{usage_volume, gain_adjustment, ramp});
}

}  // namespace media::audio
