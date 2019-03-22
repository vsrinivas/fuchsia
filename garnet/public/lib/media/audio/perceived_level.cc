// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/audio/perceived_level.h"

#include <algorithm>
#include <cmath>

#include <fuchsia/media/cpp/fidl.h>

#include "lib/fxl/logging.h"

namespace media {
namespace {

static constexpr float kUnityGainDb = 0.0f;
static constexpr float kMinLevelGainDb = -60.0f;

}  // namespace

// static
float PerceivedLevel::GainToLevel(float gain_db) {
  if (gain_db <= kMinLevelGainDb) {
    return 0.0f;
  }

  if (gain_db >= kUnityGainDb) {
    return 1.0f;
  }

  return 1.0f - gain_db / kMinLevelGainDb;
}

// static
float PerceivedLevel::LevelToGain(float level) {
  if (level <= 0.0f) {
    return fuchsia::media::audio::MUTED_GAIN_DB;
  }

  if (level >= 1.0f) {
    return kUnityGainDb;
  }

  return (1.0f - level) * kMinLevelGainDb;
}

// static
int PerceivedLevel::GainToLevel(float gain_db, int max_level) {
  FXL_DCHECK(max_level > 0);

  if (gain_db <= kMinLevelGainDb) {
    return 0;
  }

  if (gain_db >= kUnityGainDb) {
    return max_level;
  }

  return static_cast<int>(std::round(max_level * GainToLevel(gain_db)));
}

// static
float PerceivedLevel::LevelToGain(int level, int max_level) {
  FXL_DCHECK(max_level > 0);

  if (level <= 0) {
    return fuchsia::media::audio::MUTED_GAIN_DB;
  }

  if (level >= max_level) {
    return kUnityGainDb;
  }

  return LevelToGain(static_cast<float>(level) / max_level);
}

}  // namespace media
