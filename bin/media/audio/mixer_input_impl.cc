// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio/mixer_input_impl.h"

namespace media {

// MixerInputImpl<float, float, float> template specializations.

template <>
void MixerInputImpl<float, float, float>::Job::MixUnity(
    float* in,
    float* out,
    uint32_t in_channel_count,
    uint32_t out_channel_count,
    uint32_t frame_count) {
  FTL_DCHECK(from_level_ == Level<float>::Unity);
  FTL_DCHECK(to_level_ == Level<float>::Unity);
  while (frame_count) {
    *out += *in;
    in += in_channel_count;
    out += out_channel_count;
    --frame_count;
  }
}

template <>
void MixerInputImpl<float, float, float>::Job::MixConstant(
    float* in,
    float* out,
    uint32_t in_channel_count,
    uint32_t out_channel_count,
    uint32_t frame_count) {
  FTL_DCHECK(from_level_ == to_level_);
  float level = from_level_.value();
  while (frame_count) {
    *out += *in * level;
    in += in_channel_count;
    out += out_channel_count;
    --frame_count;
  }
}

template <>
void MixerInputImpl<float, float, float>::Job::MixFade(
    float* in,
    float* out,
    uint32_t in_channel_count,
    uint32_t out_channel_count,
    uint32_t frame_count,
    int64_t pts) {
  // Determine the level for the first frame. In many cases, pts will be equal
  // to from_pts_, so from_level_.value() will do. In other cases, we need to
  // interpolate.
  float level = from_level_.value();
  if (pts != from_pts_) {
    FTL_DCHECK(to_pts_ != from_pts_);
    level += (to_level_.value() - level) * static_cast<float>(pts - from_pts_) /
             static_cast<float>(to_pts_ - from_pts_);
  }

  // Determine how much the level should change per frame.
  float level_delta =
      (to_level_.value() - level) / static_cast<float>(to_pts_ - pts);

  // Perform the mix, adjusting the level for each sample.
  // TODO(dalesat): If cumulative error is a problem, break this into chunks.
  for (uint32_t remaining_frame_count = frame_count; remaining_frame_count;
       --remaining_frame_count) {
    *out += *in * level;
    level += level_delta;
    in += in_channel_count;
    out += out_channel_count;
  }

  // Update from_* values so they can be reused and any cumulative error is
  // corrected for.
  from_pts_ = pts + frame_count;
  from_level_ = Level<float>(level);

#ifndef NDEBUG
  if (from_pts_ == to_pts_) {
    // TODO(dalesat): Record and report error (level vs to_level_).
  }
#endif
}

}  // namespace media
