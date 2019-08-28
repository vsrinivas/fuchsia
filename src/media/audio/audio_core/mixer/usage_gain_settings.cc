// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/usage_gain_settings.h"

#include <fbl/algorithm.h>
#include <trace/event.h>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/mixer/constants.h"

namespace media::audio {

namespace {

// Audio gains for AudioRenderers/AudioCapturers and output devices are
// expressed as floating-point values, in decibels.
static constexpr float kUnityGainDb = 0.0f;
static constexpr float kMinGainDb = fuchsia::media::audio::MUTED_GAIN_DB;

}  // namespace

float UsageGainSettings::GetRenderUsageGain(fuchsia::media::AudioRenderUsage usage) {
  auto usage_index = fidl::ToUnderlying(usage);
  return render_usage_gain_[usage_index].load();
}

float UsageGainSettings::GetCaptureUsageGain(fuchsia::media::AudioCaptureUsage usage) {
  auto usage_index = fidl::ToUnderlying(usage);
  return capture_usage_gain_[usage_index].load();
}

void UsageGainSettings::SetRenderUsageGain(fuchsia::media::AudioRenderUsage usage, float gain_db) {
  TRACE_DURATION("audio", "UsageGainSettings::GetRenderUsageGain");
  auto usage_index = fidl::ToUnderlying(usage);
  float clamped_gain_db = fbl::clamp(gain_db, kMinGainDb, kUnityGainDb);

  render_usage_gain_[usage_index].store(clamped_gain_db);
}

void UsageGainSettings::SetCaptureUsageGain(fuchsia::media::AudioCaptureUsage usage,
                                            float gain_db) {
  TRACE_DURATION("audio", "UsageGainSettings::GetCaptureUsageGain");
  auto usage_index = fidl::ToUnderlying(usage);
  float clamped_gain_db = fbl::clamp(gain_db, kMinGainDb, kUnityGainDb);

  capture_usage_gain_[usage_index].store(clamped_gain_db);
}

void UsageGainSettings::SetRenderUsageGainAdjustment(fuchsia::media::AudioRenderUsage usage,
                                                     float gain_db) {
  TRACE_DURATION("audio", "UsageGainSettings::SetRenderUsageGainAdjustment");
  auto usage_index = fidl::ToUnderlying(usage);
  float clamped_gain_db = fbl::clamp(gain_db, kMinGainDb, kUnityGainDb);

  render_usage_gain_adjustment_[usage_index].store(clamped_gain_db);
}

void UsageGainSettings::SetCaptureUsageGainAdjustment(fuchsia::media::AudioCaptureUsage usage,
                                                      float gain_db) {
  TRACE_DURATION("audio", "UsageGainSettings::SetCaptureUsageGainAdjustment");
  auto usage_index = fidl::ToUnderlying(usage);
  float clamped_gain_db = fbl::clamp(gain_db, kMinGainDb, kUnityGainDb);

  capture_usage_gain_adjustment_[usage_index].store(clamped_gain_db);
}

float UsageGainSettings::GetRenderUsageGainAdjustment(fuchsia::media::AudioRenderUsage usage) {
  auto usage_index = fidl::ToUnderlying(usage);
  return render_usage_gain_adjustment_[usage_index].load();
}

float UsageGainSettings::GetCaptureUsageGainAdjustment(fuchsia::media::AudioCaptureUsage usage) {
  auto usage_index = fidl::ToUnderlying(usage);
  return capture_usage_gain_adjustment_[usage_index].load();
}

float UsageGainSettings::GetUsageGain(const fuchsia::media::Usage& usage) {
  TRACE_DURATION("audio", "UsageGainSettings::GetUsageGain");
  if (usage.is_render_usage()) {
    return GetRenderUsageGain(usage.render_usage()) +
           GetRenderUsageGainAdjustment(usage.render_usage());
  } else {
    return GetCaptureUsageGain(usage.capture_usage()) +
           GetCaptureUsageGainAdjustment(usage.capture_usage());
  }
}

}  // namespace media::audio
