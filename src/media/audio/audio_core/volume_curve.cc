// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "volume_curve.h"

#include <float.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>

#include "src/lib/fxl/strings/string_printf.h"
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

fit::result<VolumeCurve, std::string> VolumeCurve::FromMappings(
    std::vector<VolumeMapping> mappings) {
  if (mappings.size() < 2) {
    return fit::error("mapping must have at least two entries");
  }

  if (auto& front = mappings.front(); front.volume != fuchsia::media::audio::MIN_VOLUME ||
                                      front.gain_dbfs != fuchsia::media::audio::MUTED_GAIN_DB) {
    return fit::error(fxl::StringPrintf(
        "first entry (%.2f -> %.2f) must map volume level %.2f to muted gain_db (%.2f)",
        front.volume, front.gain_dbfs, fuchsia::media::audio::MIN_VOLUME,
        fuchsia::media::audio::MUTED_GAIN_DB));
  }

  if (auto& back = mappings.back();
      back.volume != fuchsia::media::audio::MAX_VOLUME || back.gain_dbfs != Gain::kUnityGainDb) {
    return fit::error(fxl::StringPrintf(
        "last entry (%.2f -> %.2f) must map volume level %.2f to gain_db = %.2f", back.volume,
        back.gain_dbfs, fuchsia::media::audio::MAX_VOLUME, Gain::kUnityGainDb));
  }

  for (size_t i = 1; i < mappings.size(); ++i) {
    if (mappings[i - 1].volume >= mappings[i].volume) {
      return fit::error(fxl::StringPrintf("volume mapping does not increase: %.2f is not > %.2f",
                                          mappings[i].volume, mappings[i - 1].volume));
    }

    if (mappings[i - 1].gain_dbfs >= mappings[i].gain_dbfs) {
      return fit::error(fxl::StringPrintf("gain_db mapping does not increase: %.2f is not > %.2f",
                                          mappings[i].gain_dbfs, mappings[i - 1].gain_dbfs));
    }
  }

  return fit::ok(VolumeCurve(std::move(mappings)));
}

VolumeCurve::VolumeCurve(std::vector<VolumeMapping> mappings) : mappings_(std::move(mappings)) {}

float VolumeCurve::VolumeToDb(float volume) const {
  const float x = std::clamp<float>(volume, fuchsia::media::audio::MIN_VOLUME,
                                    fuchsia::media::audio::MAX_VOLUME);

  const auto bounds = Bounds(x, Attribute::kVolume);
  FX_DCHECK(bounds.has_value())
      << "At construction, we ensure the volume domain includes [0.0, 1.0].";

  const auto [lower_bound, upper_bound] = bounds.value();

  const auto x0 = lower_bound.volume;
  const auto a = lower_bound.gain_dbfs;
  const auto x1 = upper_bound.volume;
  const auto b = upper_bound.gain_dbfs;

  FX_DCHECK(x1 != x0) << "At construction, we reject vertical segments.";

  const auto alpha = (x - x0) / (x1 - x0);

  return mixer::LinearInterpolate(a, b, alpha);
}

float VolumeCurve::DbToVolume(float gain_dbfs) const {
  const float x =
      std::clamp<float>(gain_dbfs, fuchsia::media::audio::MUTED_GAIN_DB, Gain::kUnityGainDb);

  const auto bounds = Bounds(x, Attribute::kGain);
  if (!bounds.has_value()) {
    // We verify that our volume curve tops off at Unity at construction time, so we won't be above
    // that. If our gain is below our min gain for the volume curve, we'll clamp our volume to 0.
    return 0.0;
  }

  const auto [lower_bound, upper_bound] = bounds.value();

  const auto x0 = lower_bound.gain_dbfs;
  const auto a = lower_bound.volume;
  const auto x1 = upper_bound.gain_dbfs;
  const auto b = upper_bound.volume;

  FX_DCHECK(x1 != x0) << "At construction, we reject vertical segments.";

  const auto alpha = (x - x0) / (x1 - x0);

  return mixer::LinearInterpolate(a, b, alpha);
}

std::optional<std::pair<VolumeCurve::VolumeMapping, VolumeCurve::VolumeMapping>>
VolumeCurve::Bounds(float x, VolumeCurve::Attribute attr) const {
  const auto mappings_are_enclosing_bounds = [x, attr](VolumeMapping a, VolumeMapping b) {
    if (attr == Attribute::kVolume) {
      return a.volume <= x && b.volume >= x;
    } else {
      return a.gain_dbfs <= x && b.gain_dbfs >= x;
    }
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
