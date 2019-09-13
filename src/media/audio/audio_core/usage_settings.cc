// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/usage_settings.h"

#include <fbl/algorithm.h>
#include <trace/event.h>

#include "src/media/audio/audio_core/mixer/gain.h"

namespace media::audio {

fuchsia::media::Usage UsageFrom(fuchsia::media::AudioRenderUsage render_usage) {
  fuchsia::media::Usage usage;
  usage.set_render_usage(render_usage);
  return usage;
}

fuchsia::media::Usage UsageFrom(fuchsia::media::AudioCaptureUsage capture_usage) {
  fuchsia::media::Usage usage;
  usage.set_capture_usage(capture_usage);
  return usage;
}

float UsageGainSettings::GetUsageGain(const fuchsia::media::Usage& usage) const {
  TRACE_DURATION("audio", "UsageGainSettings::GetUsageGain");
  if (usage.is_render_usage()) {
    const auto usage_index = fidl::ToUnderlying(usage.render_usage());
    return Gain::CombineGains(render_usage_gain_[usage_index],
                              render_usage_gain_adjustment_[usage_index], Gain::kUnityGainDb);
  } else {
    const auto usage_index = fidl::ToUnderlying(usage.capture_usage());
    return Gain::CombineGains(capture_usage_gain_[usage_index],
                              capture_usage_gain_adjustment_[usage_index], Gain::kUnityGainDb);
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

}  // namespace media::audio
