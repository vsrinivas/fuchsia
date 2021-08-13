// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/gain.h"

#include <lib/trace/event.h>

namespace media::audio {

// TODO(mpuryear): When we add ramping of another gain stage (dest, or a new
// stage), refactor to accept a stage index or a pointer to a ramp-struct.
void Gain::SetSourceGainWithRamp(float source_gain_db, zx::duration duration,
                                 fuchsia::media::audio::RampType ramp_type) {
  TRACE_DURATION("audio", "Gain::SetSourceGainWithRamp");
  FX_DCHECK(source_gain_db <= kMaxGainDb) << "Ramp target source_gain (" << source_gain_db
                                          << " db) cannot exceed maximum (" << kMaxGainDb << " db)";

  if (duration <= zx::nsec(0)) {
    FX_LOGS(WARNING) << "Gain(" << this << "): SetSourceGainWithRamp non-positive duration ("
                     << duration.to_usecs() << " usec); calling SetSourceGain(" << source_gain_db
                     << " dB)";
    SetSourceGain(source_gain_db);
    return;
  }

  if (source_gain_db == target_source_gain_db_) {
    if constexpr (kLogSetRamp) {
      FX_LOGS(INFO) << "Gain(" << this << "): SetSourceGainWithRamp is no-change (already "
                    << source_gain_db << " dB); " << duration.to_usecs() << "-usec ramp is ignored";
    }
    source_ramp_duration_ = zx::nsec(0);
    return;
  }

  if (source_gain_db <= kMinGainDb && target_source_gain_db_ <= kMinGainDb) {
    if constexpr (kLogSetRamp) {
      FX_LOGS(INFO) << "Gain(" << this << "): SetSourceGainWithRamp starts at ("
                    << target_source_gain_db_ << " dB) and ends at (" << source_gain_db
                    << " dB), below min gain (" << kMinGainDb << " dB); " << duration.to_usecs()
                    << "-usec ramp is ignored";
    }
    SetSourceGain(source_gain_db);
    return;
  }

  if constexpr (kLogSetRamp) {
    FX_LOGS(INFO) << "Gain(" << this << "): SetSourceGainWithRamp(" << source_gain_db << " dB, "
                  << duration.to_usecs() << " usec)";
  }

  // Start ramping.
  source_ramp_duration_ = duration;
  source_frames_ramped_ = 0;

  start_source_gain_db_ = target_source_gain_db_;
  start_source_scale_ = DbToScale(target_source_gain_db_);

  end_source_gain_db_ = source_gain_db;
  end_source_scale_ = DbToScale(source_gain_db);
}

void Gain::SetDestGainWithRamp(float dest_gain_db, zx::duration duration,
                               fuchsia::media::audio::RampType ramp_type) {
  TRACE_DURATION("audio", "Gain::SetDestGainWithRamp");
  FX_DCHECK(dest_gain_db <= kMaxGainDb) << "Ramp target dest_gain (" << dest_gain_db
                                        << " db) cannot exceed maximum (" << kMaxGainDb << " db)";

  if (duration <= zx::nsec(0)) {
    FX_LOGS(WARNING) << "Gain(" << this << "): SetDestGainWithRamp non-positive duration ("
                     << duration.to_usecs() << " usec); calling SetDestGain(" << dest_gain_db
                     << " dB)";
    SetDestGain(dest_gain_db);
    return;
  }

  if (dest_gain_db == target_dest_gain_db_) {
    if constexpr (kLogSetRamp) {
      FX_LOGS(INFO) << "Gain(" << this << "): SetDestGainWithRamp ramp is no-change (already "
                    << dest_gain_db << " dB); " << duration.to_usecs() << "-usec ramp is ignored";
    }
    dest_ramp_duration_ = zx::nsec(0);
    return;
  }

  if (dest_gain_db <= kMinGainDb && target_dest_gain_db_ <= kMinGainDb) {
    if constexpr (kLogSetRamp) {
      FX_LOGS(INFO) << "Gain(" << this << "): SetDestGainWithRamp starts at ("
                    << target_dest_gain_db_ << " dB) and ends at (" << dest_gain_db
                    << " dB), below min gain (" << min_gain_db_ << " dB); " << duration.to_usecs()
                    << "-usec ramp is ignored";
    }
    SetDestGain(dest_gain_db);
    return;
  }

  if constexpr (kLogSetRamp) {
    FX_LOGS(INFO) << "Gain(" << this << "): SetDestGainWithRamp(" << dest_gain_db << " dB, "
                  << duration.to_usecs() << " usec)";
  }

  // Start ramping.
  dest_ramp_duration_ = duration;
  dest_frames_ramped_ = 0;

  start_dest_gain_db_ = target_dest_gain_db_;
  start_dest_scale_ = DbToScale(target_dest_gain_db_);

  end_dest_gain_db_ = dest_gain_db;
  end_dest_scale_ = DbToScale(dest_gain_db);
}

void Gain::Advance(int64_t num_frames, const TimelineRate& destination_frames_per_reference_tick) {
  TRACE_DURATION("audio", "Gain::Advance");
  if (!IsRamping() || num_frames == 0) {
    return;
  }

  // If the output device's clock is not running, then it isn't possible to
  // convert from output frames to wallclock (local) time.
  FX_CHECK(destination_frames_per_reference_tick.invertible())
      << "Output clock must be running! Numerator of dest_frames/ref_tick is zero";

  int64_t total_frames_ramped;
  zx::duration ramp_duration;

  // First advance any source-gain ramps
  if (source_ramp_duration_.get() > 0) {
    source_frames_ramped_ += num_frames;

    zx::duration advance_duration =
        zx::nsec(destination_frames_per_reference_tick.Inverse().Scale(source_frames_ramped_));

    // These might get cleared; save them in case we need to display them later.
    total_frames_ramped = source_frames_ramped_;
    ramp_duration = source_ramp_duration_;

    if (source_ramp_duration_ > advance_duration) {
      // Even after this advance, some duration of source_ramp remains
      auto scale_from_ramp = static_cast<double>(advance_duration.to_nsecs()) /
                             static_cast<double>(source_ramp_duration_.to_nsecs()) *
                             (end_source_scale_ - start_source_scale_);
      AScale source_scale = static_cast<AScale>(scale_from_ramp + start_source_scale_);
      target_source_gain_db_ = ScaleToDb(source_scale);
    } else {
      // This advance takes us beyond the end of source_ramp.
      source_ramp_duration_ = zx::nsec(0);
      source_frames_ramped_ = 0;
      target_source_gain_db_ = end_source_gain_db_;
    }

    if constexpr (kLogRampAdvance) {
      FX_LOGS(INFO) << "Gain(" << this << ") advanced " << advance_duration.to_usecs()
                    << " usec for " << num_frames
                    << " source frames. Total frames ramped: " << total_frames_ramped << ".";
      FX_LOGS(INFO) << "source gain_db is now " << target_source_gain_db_ << " for this "
                    << ramp_duration.to_usecs() << "-usec ramp to " << end_source_gain_db_
                    << " dB.";
    }
  }

  // Then advance any dest-gain ramps
  if (dest_ramp_duration_.get() > 0) {
    dest_frames_ramped_ += num_frames;
    zx::duration advance_duration =
        zx::nsec(destination_frames_per_reference_tick.Inverse().Scale(dest_frames_ramped_));

    // These might get cleared; save them in case we need to display them later.
    total_frames_ramped = dest_frames_ramped_;
    ramp_duration = dest_ramp_duration_;

    if (dest_ramp_duration_ > advance_duration) {
      // Even after this advance, some duration of dest_ramp remains
      auto scale_from_ramp = static_cast<double>(advance_duration.to_nsecs()) /
                             static_cast<double>(dest_ramp_duration_.to_nsecs()) *
                             (end_dest_scale_ - start_dest_scale_);
      AScale dest_scale = static_cast<AScale>(scale_from_ramp + start_dest_scale_);
      target_dest_gain_db_ = ScaleToDb(dest_scale);
    } else {
      // This advance takes us beyond the end of dest_ramp.
      dest_ramp_duration_ = zx::nsec(0);
      dest_frames_ramped_ = 0;
      target_dest_gain_db_ = end_dest_gain_db_;
    }

    if constexpr (kLogRampAdvance) {
      FX_LOGS(INFO) << "Gain(" << this << ") advanced " << advance_duration.to_usecs()
                    << " usec for " << num_frames
                    << " dest frames. Total frames ramped: " << total_frames_ramped << ".";
      FX_LOGS(INFO) << "dest gain_db is now " << target_dest_gain_db_ << " for this "
                    << ramp_duration.to_usecs() << "-usec ramp to " << end_dest_gain_db_ << " dB.";
    }
  }
}

// Populate an array of gain scales, returning the max gain-scale value in the array.
// Currently we handle only SCALE_LINEAR ramps
Gain::AScale Gain::GetScaleArray(AScale* scale_arr, int64_t num_frames,
                                 const TimelineRate& destination_frames_per_reference_tick) {
  TRACE_DURATION("audio", "Gain::GetScaleArray");
  if (num_frames == 0) {
    return GetGainScale();
  }

  FX_CHECK(scale_arr != nullptr) << "Null pointer; cannot copy the array of scale values";

  if (!IsRamping()) {
    // Gain is flat for this mix job; retrieve gainscale once and set them all.
    AScale scale = GetGainScale();
    for (int64_t idx = 0; idx < num_frames; ++idx) {
      scale_arr[idx] = scale;
    }
    return scale;
  }

  // If the output device's clock is not running, then it isn't possible to
  // convert from output frames to wallclock (local) time.
  FX_CHECK(destination_frames_per_reference_tick.invertible())
      << "Output clock must be running! Numerator of dest_frames/ref_tick is zero";

  // Compose the ramp, in pieces
  RecalculateGainScale();
  TimelineRate output_to_local = destination_frames_per_reference_tick.Inverse();

  // If the source side is ramping, calculate that component
  if (source_ramp_duration_.get() > 0) {
    AScale start_source_scale = start_source_scale_;
    AScale end_source_scale = end_source_scale_;
    if (end_source_scale <= kMinScale) {
      end_source_scale = kMuteScale;
    }
    const float kInverseSourceRampDuration =
        1.0f / static_cast<float>(source_ramp_duration_.to_nsecs());

    for (int64_t idx = 0; idx < num_frames; ++idx) {
      zx::duration frame_time = zx::nsec(output_to_local.Scale(source_frames_ramped_ + idx));
      if (frame_time >= source_ramp_duration_) {
        scale_arr[idx] = end_source_scale;
      } else {
        auto ramp_fraction = static_cast<float>(frame_time.to_nsecs()) * kInverseSourceRampDuration;
        auto scale_factor =
            start_source_scale + (end_source_scale - start_source_scale) * ramp_fraction;
        scale_arr[idx] = (scale_factor <= kMinScale) ? kMuteScale : scale_factor;
      }
    }
  } else {
    // ...otherwise, the source contribution to our array is constant
    auto source_scale = DbToScale(current_source_gain_db_);
    if (source_scale <= kMinScale) {
      source_scale = kMuteScale;
    }
    for (int64_t idx = 0; idx < num_frames; ++idx) {
      scale_arr[idx] = source_scale;
    }
  }

  // If the dest side is ramping, calculate and multiply-in that component
  if (dest_ramp_duration_.get() > 0) {
    AScale start_dest_scale = start_dest_scale_;
    AScale end_dest_scale = end_dest_scale_;
    if (end_dest_scale <= kMinScale) {
      end_dest_scale = kMuteScale;
    }
    const float kInverseDestRampDuration =
        1.0f / static_cast<float>(dest_ramp_duration_.to_nsecs());

    for (int64_t idx = 0; idx < num_frames; ++idx) {
      zx::duration frame_time = zx::nsec(output_to_local.Scale(dest_frames_ramped_ + idx));
      if (frame_time >= dest_ramp_duration_) {
        scale_arr[idx] *= end_dest_scale;
      } else {
        auto ramp_fraction = static_cast<float>(frame_time.to_nsecs()) * kInverseDestRampDuration;
        auto scale_factor =
            (start_dest_scale + (end_dest_scale - start_dest_scale) * ramp_fraction);
        scale_arr[idx] *= (scale_factor <= kMinScale) ? kMuteScale : scale_factor;
      }
    }
  } else {
    // ...otherwise, the dest contribution to our array is constant
    AScale dest_scale = DbToScale(current_dest_gain_db_);
    if (dest_scale <= kMinScale) {
      dest_scale = kMuteScale;
    }
    for (int64_t idx = 0; idx < num_frames; ++idx) {
      scale_arr[idx] *= dest_scale;
    }
  }

  AScale max_scale = kMuteScale;
  // Apply gain limits; normalize sub-kMinScale values to kMuteScale; return the max scale value.
  for (int64_t idx = 0; idx < num_frames; ++idx) {
    if (scale_arr[idx] <= kMinScale) {
      scale_arr[idx] = kMuteScale;
    } else {
      scale_arr[idx] = std::clamp(scale_arr[idx], min_gain_scale_, max_gain_scale_);
    }
    max_scale = std::max(max_scale, scale_arr[idx]);
  }

  return max_scale;
}

// Calculate a stream's gain-scale multiplier from source and dest gains in
// dB. Optimize to avoid doing the full calculation unless we must.
Gain::AScale Gain::GetGainScale() {
  TRACE_DURATION("audio", "Gain::GetGainScale");

  if (IsMute()) {
    return kMuteScale;
  }

  RecalculateGainScale();

  return combined_gain_scale_;
}

// From different gain_db components, calculate gain-scale for this object.
// Mute is accounted for separately.
void Gain::RecalculateGainScale() {
  TRACE_DURATION("audio", "Gain::RecalculateGainScale");

  // If nothing changed, our previously-computed amplitude scale value is accurate.
  if ((current_source_gain_db_ == target_source_gain_db_) &&
      (current_dest_gain_db_ == target_dest_gain_db_)) {
    if constexpr (kLogGainScaleCalculation) {
      FX_LOGS(INFO) << "Gain(" << this
                    << ") retained existing combined_gain_scale_: " << combined_gain_scale_;
    }
    return;
  }

  // Something changed. Calculate combined_gain_scale_ but also cache the vals so that next time,
  // the above check can eliminate unneeded DbToScale calls.
  current_source_gain_db_ = target_source_gain_db_;
  current_dest_gain_db_ = target_dest_gain_db_;

  // We avoid DbToScale calls, with checks for Unity, Min and Max.
  //
  // If sum of the source and dest cancel each other, the combined is kUnityScale.
  if (current_dest_gain_db_ + current_source_gain_db_ == kUnityGainDb) {
    combined_gain_scale_ = kUnityScale;
  } else if (current_source_gain_db_ <= kMinGainDb || current_dest_gain_db_ <= kMinGainDb) {
    // If source or dest are at the mute point, then silence the stream.
    combined_gain_scale_ = kMuteScale;
  } else {
    float effective_gain_db = current_source_gain_db_ + current_dest_gain_db_;
    // Likewise, silence the stream if the combined gain is at the mute point.
    if (effective_gain_db <= kMinGainDb) {
      combined_gain_scale_ = kMuteScale;
    } else if (effective_gain_db >= kMaxGainDb) {
      combined_gain_scale_ = kMaxScale;
    } else {
      // Else, we really do need to compute the combined gain-scale.
      combined_gain_scale_ = DbToScale(effective_gain_db);
    }
  }

  // Apply gain limits.
  if (combined_gain_scale_ > kMuteScale) {
    combined_gain_scale_ = std::clamp(combined_gain_scale_, min_gain_scale_, max_gain_scale_);
  }

  if constexpr (kLogGainScaleCalculation) {
    FX_LOGS(INFO) << "Gain(" << this << ") new gain_scale: " << combined_gain_scale_;
  }
}

}  // namespace media::audio
