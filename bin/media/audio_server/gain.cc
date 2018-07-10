// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/gain.h"

#include <fbl/algorithm.h>
#include <math.h>

#include "lib/fxl/logging.h"

namespace media {
namespace audio {

constexpr Gain::AScale Gain::kUnityScale;
constexpr Gain::AScale Gain::kMaxScale;
constexpr float Gain::kMinGainDb;
constexpr float Gain::kMaxGainDb;

Gain::AScale Gain::GetGainScale(float output_db_gain) {
  float db_target_rend_gain = db_target_rend_gain_.load();

  // If nothing changed, return the previously-computed amplitude scale value.
  if ((db_current_rend_gain_ == db_target_rend_gain) &&
      (db_current_output_gain_ == output_db_gain)) {
    return combined_gain_scalar_;
  }

  // Update the internal gains, clamping in the process.
  db_current_rend_gain_ =
      fbl::clamp(db_target_rend_gain, kMinGainDb, kMaxGainDb);
  db_current_output_gain_ = fbl::clamp(output_db_gain, kMinGainDb, 0.0f);

  float effective_gain_db = db_current_rend_gain_ + db_current_output_gain_;

  // If either the renderer, output, or combined gain is at the force mute
  // point, just zero out the amplitude scale and return that.
  if ((db_current_rend_gain_ <= kMinGainDb) ||
      (db_current_output_gain_ <= kMinGainDb) ||
      (effective_gain_db <= kMinGainDb)) {
    combined_gain_scalar_ = 0.0f;
  } else {
    // Compute the amplitude scale factor.
    combined_gain_scalar_ = pow(10.0, effective_gain_db / 20.0);
  }

  return combined_gain_scalar_;
}

}  // namespace audio
}  // namespace media
