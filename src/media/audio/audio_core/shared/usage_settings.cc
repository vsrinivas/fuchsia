// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/usage_settings.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/media/audio/audio_core/shared/mixer/gain.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media::audio {

float UsageGainSettings::GetAdjustedUsageGain(const fuchsia::media::Usage& usage) const {
  TRACE_DURATION("audio", "UsageGainSettings::GetUsageGain");
  if (usage.is_render_usage()) {
    const auto usage_index = fidl::ToUnderlying(usage.render_usage());
    return std::min(Gain::CombineGains(render_usage_gain_[usage_index],
                                       render_usage_gain_adjustment_[usage_index]),
                    media_audio::kUnityGainDb);
  } else {
    FX_DCHECK(!usage.has_invalid_tag());
    const auto usage_index = fidl::ToUnderlying(usage.capture_usage());
    return std::min(Gain::CombineGains(capture_usage_gain_[usage_index],
                                       capture_usage_gain_adjustment_[usage_index]),
                    media_audio::kUnityGainDb);
  }
}

float UsageGainSettings::GetUnadjustedUsageGain(const fuchsia::media::Usage& usage) const {
  TRACE_DURATION("audio", "UsageGainSettings::GetUnadjustedUsageGain");
  if (usage.is_render_usage()) {
    const auto usage_index = fidl::ToUnderlying(usage.render_usage());
    return render_usage_gain_[usage_index];
  } else {
    FX_DCHECK(!usage.has_invalid_tag());
    const auto usage_index = fidl::ToUnderlying(usage.capture_usage());
    return capture_usage_gain_[usage_index];
  }
}

float UsageGainSettings::GetUsageGainAdjustment(const fuchsia::media::Usage& usage) const {
  TRACE_DURATION("audio", "UsageGainSettings::GetUsageGainAdjustment");
  if (usage.is_render_usage()) {
    const auto usage_index = fidl::ToUnderlying(usage.render_usage());
    return render_usage_gain_adjustment_[usage_index];
  } else {
    FX_DCHECK(!usage.has_invalid_tag());
    const auto usage_index = fidl::ToUnderlying(usage.capture_usage());
    return capture_usage_gain_adjustment_[usage_index];
  }
}

void UsageGainSettings::SetUsageGain(fuchsia::media::Usage usage, float gain_db) {
  TRACE_DURATION("audio", "UsageGainSettings::SetUsageGain");
  if (usage.is_render_usage()) {
    render_usage_gain_[fidl::ToUnderlying(usage.render_usage())] = gain_db;
  } else {
    capture_usage_gain_[fidl::ToUnderlying(usage.capture_usage())] = gain_db;
  }
}

void UsageGainSettings::SetUsageGainAdjustment(fuchsia::media::Usage usage, float gain_db) {
  TRACE_DURATION("audio", "UsageGainSettings::SetUsageGainAdjustment");
  if (usage.is_render_usage()) {
    render_usage_gain_adjustment_[fidl::ToUnderlying(usage.render_usage())] = gain_db;
  } else {
    capture_usage_gain_adjustment_[fidl::ToUnderlying(usage.capture_usage())] = gain_db;
  }
}

UsageVolumeSettings::UsageVolumeSettings() {
  for (auto& volume : render_usage_volume_) {
    volume = fuchsia::media::audio::MAX_VOLUME;
  }

  for (auto& volume : capture_usage_volume_) {
    volume = fuchsia::media::audio::MAX_VOLUME;
  }
}

float UsageVolumeSettings::GetUsageVolume(const fuchsia::media::Usage& usage) const {
  TRACE_DURATION("audio", "UsageVolumeSettings::GetUsageVolume");
  if (usage.is_render_usage()) {
    return render_usage_volume_[fidl::ToUnderlying(usage.render_usage())];
  } else {
    return capture_usage_volume_[fidl::ToUnderlying(usage.capture_usage())];
  }
}

void UsageVolumeSettings::SetUsageVolume(fuchsia::media::Usage usage, float volume) {
  TRACE_DURATION("audio", "UsageVolumeSettings::SetUsageVolume");
  if (usage.is_render_usage()) {
    render_usage_volume_[fidl::ToUnderlying(usage.render_usage())] = volume;
  } else {
    capture_usage_volume_[fidl::ToUnderlying(usage.capture_usage())] = volume;
  }
}

}  // namespace media::audio
