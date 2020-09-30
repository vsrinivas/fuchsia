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
#include "src/media/audio/audio_core/process_config.h"
#include "src/media/audio/audio_core/usage_settings.h"
#include "src/media/audio/audio_core/volume_control.h"

namespace media::audio {

struct Ramp {
  zx::duration duration;
  fuchsia::media::audio::RampType ramp_type;
};

// A command to realize a volume on all a stream's links.
struct VolumeCommand {
  // Volume in the range [0.0, 1.0].
  float volume;
  // A gain adjustment to be applied after volume is converted to gain for the link.
  float gain_db_adjustment;
  // A ramp with which to apply the change in volume.
  std::optional<Ramp> ramp;
};

// An interface for persisting and realizing stream volumes.
class StreamVolume {
 public:
  virtual fuchsia::media::Usage GetStreamUsage() const = 0;
  virtual bool GetStreamMute() const = 0;

  // Returns true if this stream should receive volume commands that factor in
  // transient loudness adjustments made by audio policy, such as ducking.
  //
  // Returns false if the stream should receive volume commands that
  // exclude transient loudness adjustments made by policy.
  virtual bool RespectsPolicyAdjustments() const { return true; }

  // Propagate a volume to all the stream's links.
  virtual void RealizeVolume(VolumeCommand volume_command) = 0;
};

// Manages the volume of streams, accounting for their usages.
class StreamVolumeManager {
 public:
  // Disable copy, assign, and move.
  StreamVolumeManager(StreamVolumeManager&) = delete;
  StreamVolumeManager(StreamVolumeManager&&) = delete;
  StreamVolumeManager& operator=(StreamVolumeManager) = delete;
  StreamVolumeManager& operator=(StreamVolumeManager&&) = delete;

  explicit StreamVolumeManager(async_dispatcher_t* fidl_dispatcher,
                               const RenderUsageVolumes& initial_volumes = {});

  const UsageGainSettings& GetUsageGainSettings() const;

  // Sets usage gain settings and updates affected streams.
  void SetUsageGain(fuchsia::media::Usage usage, float gain_db);
  void SetUsageGainAdjustment(fuchsia::media::Usage usage, float gain_db);

  void BindUsageVolumeClient(fuchsia::media::Usage usage,
                             fidl::InterfaceRequest<fuchsia::media::audio::VolumeControl> request);

  // Prompts the volume manager to recompute the stream's adjusted gain and send a realization
  // request.
  void NotifyStreamChanged(StreamVolume* stream_volume);
  void NotifyStreamChanged(StreamVolume* stream_volume, Ramp ramp);

  void AddStream(StreamVolume* stream_volume);
  void RemoveStream(StreamVolume* stream_volume);

 private:
  class VolumeSettingImpl final : public VolumeSetting {
   public:
    VolumeSettingImpl(fuchsia::media::Usage usage, StreamVolumeManager* owner);

    void SetVolume(float volume) override;

   private:
    fuchsia::media::Usage usage_;
    StreamVolumeManager* owner_;
  };

  void SetUsageVolume(fuchsia::media::Usage usage, float volume);

  void UpdateStreamsWithUsage(fuchsia::media::Usage usage);
  void UpdateStream(StreamVolume* stream, std::optional<Ramp> ramp);

  std::array<VolumeSettingImpl, fuchsia::media::RENDER_USAGE_COUNT>
      render_usage_volume_setting_impls_;
  std::array<VolumeSettingImpl, fuchsia::media::CAPTURE_USAGE_COUNT>
      capture_usage_volume_setting_impls_;
  std::array<VolumeControl, fuchsia::media::RENDER_USAGE_COUNT> render_usage_volume_controls_;
  std::array<VolumeControl, fuchsia::media::CAPTURE_USAGE_COUNT> capture_usage_volume_controls_;
  std::unordered_set<StreamVolume*> stream_volumes_;
  UsageGainSettings usage_gain_settings_;
  UsageVolumeSettings usage_volume_settings_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_VOLUME_MANAGER_H_
