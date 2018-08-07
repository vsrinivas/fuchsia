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

static constexpr float kUnityGain = 0.0f;
static constexpr float kMinLevelGain = -60.0f;

}  // namespace

// static
float PerceivedLevel::GainToLevel(float gain) {
  if (gain <= kMinLevelGain) {
    return 0.0f;
  }

  if (gain >= kUnityGain) {
    return 1.0f;
  }

  return 1.0f - gain / kMinLevelGain;
}

// static
float PerceivedLevel::LevelToGain(float level) {
  if (level <= 0.0f) {
    return fuchsia::media::MUTED_GAIN;
  }

  if (level >= 1.0f) {
    return kUnityGain;
  }

  return (1.0f - level) * kMinLevelGain;
}

// static
int PerceivedLevel::GainToLevel(float gain, int max_level) {
  FXL_DCHECK(max_level > 0);

  if (gain <= kMinLevelGain) {
    return 0;
  }

  if (gain >= kUnityGain) {
    return max_level;
  }

  return static_cast<int>(std::round(max_level * GainToLevel(gain)));
}

// static
float PerceivedLevel::LevelToGain(int level, int max_level) {
  FXL_DCHECK(max_level > 0);

  if (level <= 0) {
    return fuchsia::media::MUTED_GAIN;
  }

  if (level >= max_level) {
    return kUnityGain;
  }

  return LevelToGain(static_cast<float>(level) / max_level);
}

}  // namespace media
