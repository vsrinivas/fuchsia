// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/mixer/gain.h"

#include <fbl/algorithm.h>

#include "lib/fxl/logging.h"

namespace media::audio {

constexpr Gain::AScale Gain::kMinScale;
constexpr Gain::AScale Gain::kUnityScale;
constexpr Gain::AScale Gain::kMaxScale;

constexpr float Gain::kMinGainDb;
constexpr float Gain::kUnityGainDb;
constexpr float Gain::kMaxGainDb;

// TODO(mpuryear): When we add ramping of another gain stage (dest, or a new
// stage), refactor to accept a stage index or a pointer to a ramp-struct.
void Gain::SetSourceGainWithRamp(
    float source_gain_db, zx_duration_t duration_ns,
    __UNUSED fuchsia::media::audio::RampType ramp_type) {
  FXL_DCHECK(source_gain_db <= kMaxGainDb);
  FXL_DCHECK(duration_ns >= 0) << "Ramp duration cannot be negative";

  source_ramp_duration_ns_ = duration_ns;

  float current_src_gain_db = target_src_gain_db_.load();
  if (source_gain_db != current_src_gain_db) {
    if (duration_ns > 0) {
      start_src_gain_db_ = current_src_gain_db;
      start_src_scale_ = DbToScale(current_src_gain_db);

      end_src_gain_db_ = source_gain_db;
      end_src_scale_ = DbToScale(source_gain_db);
    } else {
      SetSourceGain(source_gain_db);
    }
  } else {
    // Already at the ramp destination: we are done.
    ClearSourceRamp();
  }
  if constexpr (kVerboseRampDebug) {
    FXL_LOG(INFO) << "Gain(" << this << "): SetSourceGainWithRamp("
                  << source_gain_db << " dB, " << duration_ns << " nsec)";
  }
}

void Gain::Advance(uint32_t num_frames, const TimelineRate& local_to_output) {
  if (!IsRamping() || num_frames == 0) {
    return;
  }

  // If the output device's clock is not running, then it isn't possible to
  // convert from output frames to wallclock (local) time.
  FXL_CHECK(local_to_output.invertable()) << "Output clock must be running!";

  frames_ramped_ += num_frames;
  zx_duration_t advance_ns = local_to_output.Inverse().Scale(frames_ramped_);
  float src_gain_db;

  if (source_ramp_duration_ns_ > advance_ns) {
    AScale src_scale =
        start_src_scale_ +
        (static_cast<double>(end_src_scale_ - start_src_scale_) * advance_ns) /
            source_ramp_duration_ns_;
    src_gain_db = ScaleToDb(src_scale);

  } else {
    ClearSourceRamp();
    frames_ramped_ = 0;
    src_gain_db = end_src_gain_db_;
  }

  target_src_gain_db_.store(src_gain_db);

  if constexpr (kVerboseRampDebug) {
    FXL_LOG(INFO) << "Advanced " << advance_ns << " nsec for " << num_frames
                  << " frames. Total frames ramped: " << frames_ramped_ << ".";
    FXL_LOG(INFO) << "Src_gain is now " << src_gain_db << " dB for this "
                  << source_ramp_duration_ns_ << "-nsec ramp to "
                  << end_src_gain_db_ << " dB.";
  }
}

// Populate an array of gain scales. Currently we handle only SCALE_LINEAR ramps
void Gain::GetScaleArray(AScale* scale_arr, uint32_t num_frames,
                         const TimelineRate& local_to_output) {
  if (num_frames == 0) {
    return;
  }

  FXL_CHECK(scale_arr != nullptr);

  AScale scale;
  if (src_mute_ || dest_mute_ || !IsRamping()) {
    // Gain is flat for this mix job; retrieve gainscale once and set them all.
    scale = GetGainScale();
    for (uint32_t idx = 0; idx < num_frames; ++idx) {
      scale_arr[idx] = scale;
    }
  } else {
    // If the output device's clock is not running, then it isn't possible to
    // convert from output frames to wallclock (local) time.
    FXL_CHECK(local_to_output.invertable()) << "Output clock must be running!";

    // Compose the ramp, in pieces
    TimelineRate output_to_local = local_to_output.Inverse();
    AScale dest_scale = DbToScale(target_dest_gain_db_.load());
    AScale start_scale = start_src_scale_ * dest_scale;
    AScale end_scale = end_src_scale_ * dest_scale;

    for (uint32_t idx = 0; idx < num_frames; ++idx) {
      zx_duration_t frame_time = output_to_local.Scale(frames_ramped_ + idx);
      if (frame_time >= source_ramp_duration_ns_) {
        scale_arr[idx] = end_scale;
      } else {
        scale_arr[idx] =
            start_scale +
            (static_cast<double>(end_scale - start_scale) * frame_time) /
                source_ramp_duration_ns_;
      }
    }
  }
}

// Calculate a stream's gain-scale multiplier from source and dest gains in
// dB. Optimize to avoid doing the full calculation unless we must.
Gain::AScale Gain::GetGainScale(float src_gain_db, float dest_gain_db) {
  if (src_mute_ || dest_mute_) {
    return kMuteScale;
  }

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
    combined_gain_scale_ = kMuteScale;
  } else {
    float effective_gain_db = current_src_gain_db_ + current_dest_gain_db_;
    // Likewise, silence the stream if the combined gain is at the mute point.
    if (effective_gain_db <= kMinGainDb) {
      combined_gain_scale_ = kMuteScale;
    } else if (effective_gain_db >= kMaxGainDb) {
      combined_gain_scale_ = kMaxScale;
    } else {
      // Else, we do need to compute the combined gain-scale.
      combined_gain_scale_ = DbToScale(effective_gain_db);
    }
  }

  return combined_gain_scale_;
}

}  // namespace media::audio
