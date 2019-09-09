// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_USAGE_GAIN_SETTINGS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_USAGE_GAIN_SETTINGS_H_

#include <fuchsia/media/cpp/fidl.h>

#include <atomic>

namespace media::audio {

// TODO(35491): Remove when transitioned to xunion; xunions generate these functions.
fuchsia::media::Usage UsageFrom(fuchsia::media::AudioRenderUsage render_usage);

fuchsia::media::Usage UsageFrom(fuchsia::media::AudioCaptureUsage capture_usage);

// Usage loudness settings in gain dbfs units.
class UsageGainSettings {
 public:
  UsageGainSettings() = default;

  // Gets the gain that should affect all audio elements of the given usage, taking into account
  // the category gain and adjustment.
  float GetUsageGain(const fuchsia::media::Usage& usage) const;

  void SetUsageGain(fuchsia::media::Usage usage, float gain_db);

  void SetUsageGainAdjustment(fuchsia::media::Usage usage, float gain_db);

 private:
  // TODO(36289): Determine whether mute must be tracked here

  std::atomic<float> render_usage_gain_[fuchsia::media::RENDER_USAGE_COUNT] = {};
  std::atomic<float> capture_usage_gain_[fuchsia::media::CAPTURE_USAGE_COUNT] = {};

  std::atomic<float> render_usage_gain_adjustment_[fuchsia::media::RENDER_USAGE_COUNT] = {};
  std::atomic<float> capture_usage_gain_adjustment_[fuchsia::media::CAPTURE_USAGE_COUNT] = {};
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_USAGE_GAIN_SETTINGS_H_
