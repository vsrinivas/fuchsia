// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/mixer/gain.h"

#include <fbl/algorithm.h>
#include <math.h>

#include "lib/fxl/logging.h"

namespace media {
namespace audio {

constexpr Gain::AScale Gain::kUnityScale;
constexpr Gain::AScale Gain::kMaxScale;
constexpr float Gain::kMinGainDb;
constexpr float Gain::kMaxGainDb;

// Calculate a stream's gain-scale multiplier from source and dest gains in dB.
// Use a few optimizations to avoid doing the full calculation unless we must.
Gain::AScale Gain::GetGainScale(float src_gain_db, float dest_gain_db) {
  // If nothing changed, return the previously-computed amplitude scale value.
  if ((current_src_gain_db_ == src_gain_db) &&
      (current_dest_gain_db_ == dest_gain_db)) {
    return combined_gain_scale_;
  }

  // Update the internal gains, clamping in the process.
  // We only clamp these to kMaxGainDb, despite the fact that master (or device)
  // gain is limited to a max of 0 dB. This is because the roles played by
  // src_gain and dest_gain during playback are reversed during capture (i.e.
  // during capture the master/device gain is the src_gain).
  current_src_gain_db_ = fbl::clamp(src_gain_db, kMinGainDb, kMaxGainDb);
  current_dest_gain_db_ = fbl::clamp(dest_gain_db, kMinGainDb, kMaxGainDb);

  // If source and dest gains cancel each other, the combined is kUnityScale.
  if (current_dest_gain_db_ == -current_src_gain_db_) {
    combined_gain_scale_ = kUnityScale;
  } else if ((current_src_gain_db_ <= kMinGainDb) ||
             (current_dest_gain_db_ <= kMinGainDb)) {
    // If source or dest are at the mute point, then silence the stream.
    combined_gain_scale_ = 0.0f;
  } else {
    float effective_gain_db = current_src_gain_db_ + current_dest_gain_db_;
    // Likewise, silence the stream if the combined gain is at the mute point.
    if (effective_gain_db <= kMinGainDb) {
      combined_gain_scale_ = 0.0f;
    } else if (effective_gain_db >= kMaxGainDb) {
      combined_gain_scale_ = kMaxScale;
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
