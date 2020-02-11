// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "volume_curve.h"

#include <float.h>
#include <fuchsia/media/cpp/fidl.h>

#include <algorithm>

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/mixer/mixer_utils.h"
#include "src/media/audio/audio_core/process_config_loader.h"

namespace media::audio {

VolumeCurve VolumeCurve::DefaultForMinGain(float min_gain_db) {
  FX_DCHECK(min_gain_db < Gain::kUnityGainDb);
  FX_DCHECK(min_gain_db >= fuchsia::media::audio::MUTED_GAIN_DB);

  std::vector<VolumeMapping> mappings = {
      {fuchsia::media::audio::MIN_VOLUME, fuchsia::media::audio::MUTED_GAIN_DB}};
  if (min_gain_db != fuchsia::media::audio::MUTED_GAIN_DB) {
    mappings.push_back({FLT_EPSILON, min_gain_db});
  }
  mappings.push_back({fuchsia::media::audio::MAX_VOLUME, Gain::kUnityGainDb});

  auto curve_result = VolumeCurve::FromMappings(std::move(mappings));
  if (!curve_result.is_ok()) {
    FX_LOGS(FATAL) << "Failed to build curve; error: " << curve_result.take_error();
  }

  return curve_result.take_value();
}

fit::result<VolumeCurve, VolumeCurve::Error> VolumeCurve::FromMappings(
    std::vector<VolumeMapping> mappings) {
  if (mappings.size() < 2) {
    return fit::error(kLessThanTwoMappingsCannotMakeCurve);
  }

  if (mappings.front().volume != fuchsia::media::audio::MIN_VOLUME ||
      mappings.back().volume != fuchsia::media::audio::MAX_VOLUME) {
    return fit::error(kDomain0to1NotCovered);
  }

  if (mappings.back().gain_dbfs != Gain::kUnityGainDb) {
    return fit::error(kRange0NotCovered);
  }

  for (size_t i = 1; i < mappings.size(); ++i) {
    if (mappings[i - 1].volume >= mappings[i].volume) {
      return fit::error(kNonIncreasingDomainIllegal);
    }

    if (mappings[i - 1].gain_dbfs >= mappings[i].gain_dbfs) {
      return fit::error(kNonIncreasingRangeIllegal);
    }
  }

  return fit::ok(VolumeCurve(std::move(mappings)));
}

VolumeCurve::VolumeCurve(std::vector<VolumeMapping> mappings) : mappings_(std::move(mappings)) {}

float VolumeCurve::VolumeToDb(float volume) const {
  const float x = std::clamp<float>(volume, fuchsia::media::audio::MIN_VOLUME,
                                    fuchsia::media::audio::MAX_VOLUME);

  const auto bounds = Bounds(x);
  FX_DCHECK(bounds.has_value())
      << "At construction, we ensure the volume domain includes [0.0, 1.0].";

  const auto [lower_bound, upper_bound] = bounds.value();

  const auto x0 = lower_bound.volume;
  const auto a = lower_bound.gain_dbfs;
  const auto x1 = upper_bound.volume;
  const auto b = upper_bound.gain_dbfs;

  FX_DCHECK(x1 != x0) << "At construction, we reject vertical segments.";

  const auto alpha = (x - x0) / (x1 - x0);

  return mixer::LinearInterpolateF(a, b, alpha);
}

std::optional<std::pair<VolumeCurve::VolumeMapping, VolumeCurve::VolumeMapping>>
VolumeCurve::Bounds(float x) const {
  const auto mappings_are_enclosing_bounds = [x](VolumeMapping a, VolumeMapping b) {
    return a.volume <= x && b.volume >= x;
  };

  auto it = std::adjacent_find(mappings_.begin(), mappings_.end(), mappings_are_enclosing_bounds);
  if (it == mappings_.end()) {
    return std::nullopt;
  }

  const auto lower_bound = *it;
  const auto upper_bound = *(++it);
  return {{lower_bound, upper_bound}};
}

}  // namespace media::audio
