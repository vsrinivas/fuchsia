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

void StreamVolumeManager::NotifyStreamChanged(StreamGain* stream_gain) {
  UpdateStream(stream_gain, std::nullopt);
}

void StreamVolumeManager::NotifyStreamChanged(StreamGain* stream_gain, Ramp ramp) {
  UpdateStream(stream_gain, ramp);
}

void StreamVolumeManager::AddStream(StreamGain* stream_gain) { stream_gains_.insert(stream_gain); }
void StreamVolumeManager::RemoveStream(StreamGain* stream_gain) {
  stream_gains_.erase(stream_gain);
}

void StreamVolumeManager::UpdateStreamsWithUsage(fuchsia::media::Usage usage) {
  for (auto& stream : stream_gains_) {
    if (fidl::Equals(stream->GetStreamUsage(), usage)) {
      UpdateStream(stream, std::nullopt);
    }
  }
}

void StreamVolumeManager::UpdateStream(StreamGain* stream, std::optional<Ramp> ramp) {
  if (stream->GetStreamMute()) {
    stream->RealizeAdjustedGain(fuchsia::media::audio::MUTED_GAIN_DB, ramp);
    return;
  }

  const auto stream_gain = stream->GetStreamGain();
  const auto usage = stream->GetStreamUsage();

  const auto usage_gain = usage_gain_settings_.GetUsageGain(std::move(usage));
  const auto adjusted_gain = Gain::CombineGains(stream_gain, usage_gain);

  stream->RealizeAdjustedGain(adjusted_gain, ramp);
}

}  // namespace media::audio
