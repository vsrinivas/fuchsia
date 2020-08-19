// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/gain.h"

#include <lib/trace/event.h>

namespace media::audio {

// TODO(mpuryear): When we add ramping of another gain stage (dest, or a new
// stage), refactor to accept a stage index or a pointer to a ramp-struct.
void Gain::SetSourceGainWithRamp(float source_gain_db, zx::duration duration,
                                 __UNUSED fuchsia::media::audio::RampType ramp_type) {
  TRACE_DURATION("audio", "Gain::SetSourceGainWithRamp");
  FX_DCHECK(source_gain_db <= kMaxGainDb);
  FX_DCHECK(duration.get() >= 0) << "Ramp duration cannot be negative";

  if (duration <= zx::nsec(0)) {
    SetSourceGain(source_gain_db);
    return;
  }

  if (source_gain_db != target_src_gain_db_) {
    // Start ramping.
    source_ramp_duration_ = duration;
    frames_ramped_ = 0;

    start_src_gain_db_ = target_src_gain_db_;
    start_src_scale_ = DbToScale(target_src_gain_db_);

    end_src_gain_db_ = source_gain_db;
    end_src_scale_ = DbToScale(source_gain_db);
  } else {
    // Already at the ramp destination: we are done.
    source_ramp_duration_ = zx::nsec(0);
  }

  if constexpr (kVerboseRampDebug) {
    FX_LOGS(INFO) << "Gain(" << this << "): SetSourceGainWithRamp(" << source_gain_db << " dB, "
                  << duration.to_nsecs() << " nsec)";
  }
}

void Gain::Advance(uint32_t num_frames, const TimelineRate& destination_frames_per_reference_tick) {
  TRACE_DURATION("audio", "Gain::Advance");
  if (!IsRamping() || num_frames == 0) {
    return;
  }

  // If the output device's clock is not running, then it isn't possible to
  // convert from output frames to wallclock (local) time.
  FX_CHECK(destination_frames_per_reference_tick.invertible()) << "Output clock must be running!";

  frames_ramped_ += num_frames;
  zx::duration advance_duration =
      zx::nsec(destination_frames_per_reference_tick.Inverse().Scale(frames_ramped_));
  float src_gain_db;

  if (source_ramp_duration_ > advance_duration) {
    AScale src_scale = start_src_scale_ + (static_cast<double>(end_src_scale_ - start_src_scale_) *
                                           advance_duration.to_nsecs()) /
                                              source_ramp_duration_.to_nsecs();
    src_gain_db = ScaleToDb(src_scale);

  } else {
    source_ramp_duration_ = zx::nsec(0);
    frames_ramped_ = 0;
    src_gain_db = end_src_gain_db_;
  }

  target_src_gain_db_ = src_gain_db;

  if constexpr (kVerboseRampDebug) {
    FX_LOGS(INFO) << "Advanced " << advance_duration.to_nsecs() << " nsec for " << num_frames
                  << " frames. Total frames ramped: " << frames_ramped_ << ".";
    FX_LOGS(INFO) << "Src_gain is now " << src_gain_db << " dB for this "
                  << source_ramp_duration_.to_nsecs() << "-nsec ramp to " << end_src_gain_db_
                  << " dB.";
  }
}

// Populate an array of gain scales. Currently we handle only SCALE_LINEAR ramps
void Gain::GetScaleArray(AScale* scale_arr, uint32_t num_frames,
                         const TimelineRate& destination_frames_per_reference_tick) {
  TRACE_DURATION("audio", "Gain::GetScaleArray");
  if (num_frames == 0) {
    return;
  }

  FX_CHECK(scale_arr != nullptr);

  AScale scale;
  if (!IsRamping()) {
    // Gain is flat for this mix job; retrieve gainscale once and set them all.
    scale = GetGainScale();
    for (uint32_t idx = 0; idx < num_frames; ++idx) {
      scale_arr[idx] = scale;
    }
  } else {
    // If the output device's clock is not running, then it isn't possible to
    // convert from output frames to wallclock (local) time.
    FX_CHECK(destination_frames_per_reference_tick.invertible()) << "Output clock must be running!";

    // Compose the ramp, in pieces
    TimelineRate output_to_local = destination_frames_per_reference_tick.Inverse();
    AScale dest_scale = DbToScale(target_dest_gain_db_);
    AScale start_scale = start_src_scale_ * dest_scale;
    AScale end_scale = end_src_scale_ * dest_scale;

    for (uint32_t idx = 0; idx < num_frames; ++idx) {
      zx::duration frame_time = zx::nsec(output_to_local.Scale(frames_ramped_ + idx));
      if (frame_time >= source_ramp_duration_) {
        scale_arr[idx] = end_scale;
      } else {
        scale_arr[idx] =
            start_scale + (static_cast<double>(end_scale - start_scale) * frame_time.to_nsecs()) /
                              source_ramp_duration_.to_nsecs();
      }
    }
  }
}

// Calculate a stream's gain-scale multiplier from source and dest gains in
// dB. Optimize to avoid doing the full calculation unless we must.
Gain::AScale Gain::GetGainScale(float src_gain_db, float dest_gain_db) {
  TRACE_DURATION("audio", "Gain::GetGainScale");

  // If nothing changed, return the previously-computed amplitude scale value.
  if ((current_src_gain_db_ == src_gain_db) && (current_dest_gain_db_ == dest_gain_db)) {
    return combined_gain_scale_;
  }

  current_src_gain_db_ = src_gain_db;
  current_dest_gain_db_ = dest_gain_db;

  // If sum of the src and dest cancel each other, the combined is kUnityScale.
  if ((current_dest_gain_db_ + current_src_gain_db_) == kUnityGainDb) {
    combined_gain_scale_ = kUnityScale;
  } else if ((current_src_gain_db_ <= kMinGainDb) || (current_dest_gain_db_ <= kMinGainDb)) {
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
