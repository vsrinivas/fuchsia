// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_GAIN_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_GAIN_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <stdint.h>

#include <algorithm>
#include <cmath>

#include "src/media/audio/audio_core/shared/mixer/constants.h"
#include "src/media/audio/audio_core/shared/mixer/logging_flags.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media::audio {

// A class containing factors used for software scaling in the mixer pipeline.
// Not thread safe.
class Gain {
 public:
  // Amplitude scale factors are expressed as 32-bit IEEE-754 floating point.
  using AScale = float;
  static constexpr AScale kMuteScale = 0.0f;

  static inline float CombineGains(float gain_db_a, float gain_db_b) {
    if (gain_db_a > media_audio::kMinGainDb && gain_db_b > media_audio::kMinGainDb) {
      return std::max(gain_db_a + gain_db_b, media_audio::kMinGainDb);
    }
    return media_audio::kMinGainDb;
  }

  struct Limits {
    std::optional<float> min_gain_db;
    std::optional<float> max_gain_db;
  };

  Gain() : Gain(Limits{}) {}

  explicit Gain(Limits limits)
      : min_gain_db_(std::max(limits.min_gain_db.value_or(media_audio::kMinGainDb),
                              media_audio::kMinGainDb)),
        max_gain_db_(limits.max_gain_db.value_or(std::numeric_limits<float>::max())),
        min_gain_scale_(media_audio::DbToScale(min_gain_db_)),
        max_gain_scale_(media_audio::DbToScale(max_gain_db_)) {
    if constexpr (kLogGainScaleCalculation) {
      FX_LOGS(INFO) << "Gain(" << this << ") created with min_gain_scale_: " << min_gain_scale_
                    << ", max_gain_scale_: " << max_gain_scale_;
    }
  }

  // Retrieves the overall gain-scale, combining the Source, Dest, and Adjustment controls.
  AScale GetGainScale();
  float GetGainDb() { return media_audio::ScaleToDb(GetGainScale()); }

  // Retrieves the overall gain-scale, combining the Source and Dest controls only.
  AScale GetUnadjustedGainScale();
  float GetUnadjustedGainDb() { return media_audio::ScaleToDb(GetUnadjustedGainScale()); }

  // Calculates and return an array of gain-scale values for the next `num_frames`.
  //
  // The calculation is performed in two steps: First, the Source and Dest controls are combined and
  // the maximum value is saved. Second, the Adjustment control is added. The return value is the
  // max value computed in the first type (the max value from the combination of Source and Dest).
  AScale CalculateScaleArray(AScale* scale_arr, int64_t num_frames, const TimelineRate& rate);

  // Returns the current gain from each control, including mute effects.
  float GetSourceGainDb() const {
    return source_.IsMuted() ? media_audio::kMinGainDb
                             : std::max(source_.GainDb(), media_audio::kMinGainDb);
  }
  float GetDestGainDb() const {
    return dest_.IsMuted() ? media_audio::kMinGainDb
                           : std::max(dest_.GainDb(), media_audio::kMinGainDb);
  }
  float GetGainAdjustmentDb() const {
    return adjustment_.IsMuted() ? media_audio::kMinGainDb
                                 : std::max(adjustment_.GainDb(), media_audio::kMinGainDb);
  }

  // These functions determine which performance-optimized templatized functions we use for a Mix.
  // Thus they include knowledge about the foreseeable future (e.g. ramping).
  //
  // IsSilent:      Muted OR (current gain is silent AND not ramping toward >kMinGainDb).
  // IsUnity:       Current gain == kUnityGainDb AND not ramping.
  // IsRamping:     Remaining ramp duration > 0 AND not muted.
  //
  bool IsSilent() {
    return source_.IsMuted() || dest_.IsMuted() || adjustment_.IsMuted() ||
           // source is currently silent and not ramping up
           (source_.GainDb() <= media_audio::kMinGainDb && !source_.IsRampingUp()) ||
           // or dest is currently silent and not ramping up
           (dest_.GainDb() <= media_audio::kMinGainDb && !dest_.IsRampingUp()) ||
           // or adjustment is currently silent and not ramping up
           (adjustment_.GainDb() <= media_audio::kMinGainDb && !adjustment_.IsRampingUp()) ||
           // or the combination is silent and neither is ramping up
           (source_.GainDb() + dest_.GainDb() + adjustment_.GainDb() <= media_audio::kMinGainDb &&
            !source_.IsRampingUp() && !dest_.IsRampingUp() && !adjustment_.IsRampingUp());
  }

  bool IsUnity() {
    return !source_.IsMuted() && !dest_.IsMuted() && !adjustment_.IsMuted() &&
           !source_.IsRamping() && !dest_.IsRamping() && !adjustment_.IsRamping() &&
           (source_.GainDb() + dest_.GainDb() + adjustment_.GainDb() ==
            media_audio::kUnityGainDb) &&
           (min_gain_db_ <= media_audio::kUnityGainDb) &&
           (max_gain_db_ >= media_audio::kUnityGainDb);
  }

  bool IsRamping() {
    return !source_.IsMuted() && !dest_.IsMuted() && !adjustment_.IsMuted() &&
           (source_.IsRamping() || dest_.IsRamping() || adjustment_.IsRamping());
  }

  // Manipulates the Source control. This is the only control where Mute is currently needed/used.
  void SetSourceGain(float gain_db) { source_.SetGain(gain_db); }
  void SetSourceMute(bool mute) { source_.SetMute(mute); }

  void SetSourceGainWithRamp(
      float gain_db, zx::duration duration,
      fuchsia::media::audio::RampType ramp_type = fuchsia::media::audio::RampType::SCALE_LINEAR) {
    source_.SetGainWithRamp(gain_db, duration, ramp_type);
  }

  void CompleteSourceRamp() {
    if constexpr (kLogGainRampAdvance) {
      FX_LOGS(INFO) << "Gain(" << this << "): " << __func__;
    }
    source_.CompleteRamp();
  }

  // Manipulates the Dest control.
  void SetDestGain(float gain_db) { dest_.SetGain(gain_db); }

  void SetDestGainWithRamp(
      float gain_db, zx::duration duration,
      fuchsia::media::audio::RampType ramp_type = fuchsia::media::audio::RampType::SCALE_LINEAR) {
    dest_.SetGainWithRamp(gain_db, duration, ramp_type);
  }

  void CompleteDestRamp() {
    if constexpr (kLogGainRampAdvance) {
      FX_LOGS(INFO) << __func__;
    }
    dest_.CompleteRamp();
  }

  // Manipulates the Adjustment control.
  void SetGainAdjustment(float gain_db) { adjustment_.SetGain(gain_db); }

  void SetGainAdjustmentWithRamp(
      float gain_db, zx::duration duration,
      fuchsia::media::audio::RampType ramp_type = fuchsia::media::audio::RampType::SCALE_LINEAR) {
    adjustment_.SetGainWithRamp(gain_db, duration, ramp_type);
  }

  void CompleteAdjustmentRamp() { adjustment_.CompleteRamp(); }

  // Advances the state of all in-progress ramps by the specified number of frames.
  void Advance(int64_t num_frames, const TimelineRate& rate) {
    source_.Advance(num_frames, rate);
    dest_.Advance(num_frames, rate);
    adjustment_.Advance(num_frames, rate);
  }

 private:
  const float min_gain_db_;
  const float max_gain_db_;
  const float min_gain_scale_;
  const float max_gain_scale_;

  // A single gain control can be muted, set to a fixed value, or ramping.
  class Control {
   public:
    explicit Control(std::string_view name) : name_(name) {}

    float GainDb() const { return gain_db_; }
    bool IsMuted() const { return mute_; }
    bool IsRamping() const { return !IsMuted() && ramp_duration_ > zx::nsec(0); }
    bool IsRampingUp() const { return IsRamping() && ramp_start_gain_db_ < ramp_end_gain_db_; }
    bool IsRampingDown() const { return IsRamping() && ramp_start_gain_db_ > ramp_end_gain_db_; }

    void SetGain(float gain_db) {
      if constexpr (kLogGainSetGainCalls) {
        FX_LOGS(INFO) << "Gain(" << this << "): " << name_ << ".SetGain(" << gain_db
                      << "), was gain_db " << gain_db_ << ", start_db " << ramp_start_gain_db_
                      << ", end_db " << ramp_end_gain_db_;
      }
      ramp_duration_ = zx::nsec(0);
      gain_db_ = gain_db;
    }

    void SetMute(bool mute) {
      if constexpr (kLogGainSetMute) {
        FX_LOGS(INFO) << "Gain(" << this << "): " << name_ << ".SetMute(" << std::boolalpha << mute
                      << "), was " << mute_;
      }
      mute_ = mute;
    }

    void SetGainWithRamp(float gain_db, zx::duration duration,
                         fuchsia::media::audio::RampType ramp_type);

    void CompleteRamp() {
      if constexpr (kLogGainRampAdvance) {
        FX_LOGS(INFO) << "Gain(" << this << "): " << name_ << ".CompleteRamp()";
      }
      if (ramp_duration_ != zx::nsec(0)) {
        ramp_duration_ = zx::nsec(0);
        SetGain(ramp_end_gain_db_);
      }
    }

    void Advance(int64_t num_frames, const TimelineRate& rate);

    // Caller must fill scale_arr with initial values (e.g. 1.0). The Control must be ramping.
    void AccumulateScaleArrayForRamp(
        AScale* scale_arr, int64_t num_frames,
        const TimelineRate& destination_frames_per_reference_tick) const;

   private:
    // For debugging only.
    std::string name_;

    // Current gain value.
    float gain_db_ = media_audio::kUnityGainDb;
    bool mute_ = false;

    // A linear ramp from ramp_start_scale_ to ramp_end_scale_ over ramp_duration_.
    float ramp_start_scale_ = media_audio::kUnityGainScale;
    float ramp_start_gain_db_ = media_audio::kUnityGainDb;
    float ramp_end_scale_ = media_audio::kUnityGainScale;
    float ramp_end_gain_db_ = media_audio::kUnityGainDb;

    zx::duration ramp_duration_;        // if zero, we are not ramping
    int64_t frames_ramped_so_far_ = 0;  // how many frames ramped so far
  };

  Control source_{"source"};
  Control dest_{"dest"};
  Control adjustment_{"adjustment"};

  float latest_scale_ = 100.0f;  // Guaranteed to not match the first set value.
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_MIXER_GAIN_H_
