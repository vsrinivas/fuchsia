// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/gain.h"

#include <fbl/algorithm.h>
#include <math.h>

#include "lib/fxl/logging.h"

namespace media {
namespace audio {

constexpr Gain::AScale Gain::kUnityScale;
constexpr Gain::AScale Gain::kMaxScale;
constexpr float Gain::kMinGainDb;
constexpr float Gain::kMaxGainDb;

// Calculate a stream's gain-scale multiplier, given an output gain (in db).
// Use a few optimizations to avoid doing the full calculation unless we must.
Gain::AScale Gain::GetGainScale(float output_db_gain) {
  float db_target_rend_gain = db_target_rend_gain_.load();

  // If nothing changed, return the previously-computed amplitude scale value.
  if ((db_current_rend_gain_ == db_target_rend_gain) &&
      (db_current_output_gain_ == output_db_gain)) {
    return combined_gain_scale_;
  }

  // Update the internal gains, clamping in the process.
  db_current_rend_gain_ =
      fbl::clamp(db_target_rend_gain, kMinGainDb, kMaxGainDb);
  db_current_output_gain_ = fbl::clamp(output_db_gain, kMinGainDb, 0.0f);

  // If output and render gains cancel each other, the combined is kUnityScale.
  if (db_current_output_gain_ == -db_current_rend_gain_) {
    combined_gain_scale_ = kUnityScale;
  } else if ((db_current_rend_gain_ <= kMinGainDb) ||
             (db_current_output_gain_ <= kMinGainDb)) {
    // If render or output is at the mute point, then silence the stream.
    combined_gain_scale_ = 0.0f;
  } else {
    float effective_gain_db = db_current_rend_gain_ + db_current_output_gain_;
    // Likewise, silence the stream if the combined gain is at the mute point.
    if (effective_gain_db <= kMinGainDb) {
      combined_gain_scale_ = 0.0f;
    } else {
      // Else, we do need to compute the combined gain-scale. Note: multiply-by-
      // .05 equals divide-by-20 -- and is faster on non-optimized builds.
      // Note: 0.05 must be double (not float), for the precision we require.
      combined_gain_scale_ = pow(10.0f, effective_gain_db * 0.05);
    }
  }

  return combined_gain_scale_;
}

}  // namespace audio
}  // namespace media
