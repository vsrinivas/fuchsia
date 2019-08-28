// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_USAGE_GAIN_SETTINGS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_USAGE_GAIN_SETTINGS_H_

#include <atomic>

#include <fuchsia/media/cpp/fidl.h>

namespace media::audio {

// A class containing gain settings for audio usages.
class UsageGainSettings {
 public:
  UsageGainSettings() = default;

  void SetRenderUsageGain(fuchsia::media::AudioRenderUsage usage, float gain_db);
  void SetCaptureUsageGain(fuchsia::media::AudioCaptureUsage usage, float gain_db);

  void SetRenderUsageGainAdjustment(fuchsia::media::AudioRenderUsage usage, float gain_db);
  void SetCaptureUsageGainAdjustment(fuchsia::media::AudioCaptureUsage usage, float gain_db);

  // Gets the gain that should affect all audio elements of the given usage, taking into account the
  // category gain and adjustment.
  float GetUsageGain(const fuchsia::media::Usage& usage);

 private:
  float GetRenderUsageGain(fuchsia::media::AudioRenderUsage usage);
  float GetCaptureUsageGain(fuchsia::media::AudioCaptureUsage usage);

  float GetRenderUsageGainAdjustment(fuchsia::media::AudioRenderUsage usage);
  float GetCaptureUsageGainAdjustment(fuchsia::media::AudioCaptureUsage usage);

  std::atomic<float> render_usage_gain_[fuchsia::media::RENDER_USAGE_COUNT] = {};
  std::atomic<float> capture_usage_gain_[fuchsia::media::CAPTURE_USAGE_COUNT] = {};

  std::atomic<float> render_usage_gain_adjustment_[fuchsia::media::RENDER_USAGE_COUNT] = {};
  std::atomic<float> capture_usage_gain_adjustment_[fuchsia::media::CAPTURE_USAGE_COUNT] = {};
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_USAGE_GAIN_SETTINGS_H_
