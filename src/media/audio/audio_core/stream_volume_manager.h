// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_VOLUME_MANAGER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_VOLUME_MANAGER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>

#include <optional>
#include <unordered_set>

#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/mixer/usage_gain_settings.h"

namespace media::audio {

struct Ramp {
  zx_duration_t duration_ns;
  fuchsia::media::audio::RampType ramp_type;
};

// An interface for persisting and realizing stream volumes.
// TODO(35392): Change to StreamVolume, keep volume here.
class StreamGain {
 public:
  virtual float GetStreamGain() const = 0;
  virtual fuchsia::media::Usage GetStreamUsage() const = 0;
  virtual bool GetStreamMute() const = 0;

  // Propagate an adjusted gain value to all the stream's links.
  virtual void RealizeAdjustedGain(float gain_db, std::optional<Ramp> ramp) = 0;
};

// Manages the volume of streams, accounting for their usages.
class StreamVolumeManager {
 public:
  // Disable copy, assign, and move.
  StreamVolumeManager(StreamVolumeManager&) = delete;
  StreamVolumeManager(StreamVolumeManager&&) = delete;
  StreamVolumeManager& operator=(StreamVolumeManager) = delete;
  StreamVolumeManager& operator=(StreamVolumeManager&&) = delete;

  StreamVolumeManager() = default;

  const UsageGainSettings& GetUsageGainSettings() const;

  // Sets usage gain settings and updates affected streams.
  void SetUsageGain(fuchsia::media::Usage usage, float gain_db);
  void SetUsageGainAdjustment(fuchsia::media::Usage usage, float gain_db);

  // Prompts the volume manager to recompute the stream's adjusted gain and send a realization
  // request.
  void NotifyStreamChanged(StreamGain* stream_gain);
  void NotifyStreamChanged(StreamGain* stream_gain, Ramp ramp);

  void AddStream(StreamGain* stream_gain);
  void RemoveStream(StreamGain* stream_gain);

 private:
  void UpdateStreamsWithUsage(fuchsia::media::Usage usage);
  void UpdateStream(StreamGain* stream, std::optional<Ramp> ramp);

  std::unordered_set<StreamGain*> stream_gains_;
  UsageGainSettings usage_gain_settings_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_VOLUME_MANAGER_H_
