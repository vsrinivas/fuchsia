// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_GAIN_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_GAIN_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <stdint.h>

#include <algorithm>
#include <cmath>

#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media::audio {

constexpr bool kLogSetGain = false;
constexpr bool kLogSetMute = false;
constexpr bool kLogSetRamp = false;
constexpr bool kLogRampAdvance = false;
constexpr bool kLogGainScaleCalculation = false;

// A class containing factors used for software scaling in the mixer pipeline.
// Not thread safe.
class Gain {
 public:
  // Audio gains for AudioRenderers/AudioCapturers and output devices are
  // expressed as floating-point values, in decibels. For each signal path, two
  // gain values are combined and then stored in the API-to-device link (usually
  // AudioRenderer-to-output), as a 32-bit floating-point amplitude multiplier.
  //
  // Playback example: source (renderer) gain + dest (device) gain = total gain.
  // Capture example: source (device) gain + dest (capturer) gain = total gain.
  static constexpr float kMaxGainDb = fuchsia::media::audio::MAX_GAIN_DB;
  static constexpr float kUnityGainDb = 0.0f;
  static constexpr float kMinGainDb = fuchsia::media::audio::MUTED_GAIN_DB;

  // Amplitude scale factors are expressed as 32-bit IEEE-754 floating point.
  using AScale = float;

  // Note: multiply-by-.05 equals divide-by-20 -- and is faster on most
  // builds. Note: 0.05 must be double (not float) for the required precision.
  static AScale DbToScale(float gain_db) { return static_cast<AScale>(pow(10.0f, gain_db * 0.05)); }
  static float ScaleToDb(AScale scale) {
    return std::clamp(std::log10(scale) * 20.0f, kMinGainDb, kMaxGainDb);
  }
  // Higher-precision (but slower) version currently used only by fidelity tests
  static double DoubleToDb(double val) { return std::log10(val) * 20.0; }
  static inline float CombineGains(float gain_db_a, float gain_db_b,
                                   float max_gain_db = kMaxGainDb) {
    if (gain_db_a <= kMinGainDb || gain_db_b <= kMinGainDb) {
      return kMinGainDb;
    }

    return std::clamp(gain_db_a + gain_db_b, kMinGainDb, max_gain_db);
  }

  // Helper constant values in the gain-scale domain.
  //
  // kMinScale is the value at which the amplitude scaler is guaranteed to drive
  // all sample values to a value of 0 (meaning we waste compute cycles if we
  // actually scale anything). We normalize all input formats to the same
  // full-scale bounds, so this value is identical for all input types. The
  // calculation of this value takes rounding into account.
  //
  // kUnityScale is the scale value at which mix inputs are passed bit-for-bit
  // through the mixer into the accumulation buffer. This is used during the Mix
  // process as an optimization, to avoid unnecessary multiplications.
  //
  // kMaxScale is the scale corresponding to the largest allowed gainDb value,
  // currently +24.0 decibels. Scales above this value will be clamped to this.
  static constexpr AScale kMuteScale = 0.0f;
  static constexpr AScale kMinScale = 0.00000001f;  // kMinGainDb is -160.0 dB
  static constexpr AScale kUnityScale = 1.0f;
  static constexpr AScale kMaxScale = 15.8489319f;  // kMaxGainDb is +24.0 dB

  // The final (combined) gain is limited to the range [kMinGainDb, kMaxGainDb]
  // by default, but a more restricted range can be given in this constructor.
  // No matter the value of min_gain_db, the gain can always be set to MUTED_GAIN_DB,
  // either explicitly or via Set{Source,Dest}Mute().
  struct Limits {
    std::optional<float> min_gain_db;
    std::optional<float> max_gain_db;
  };

  Gain() : Gain(Limits{}) {}

  explicit Gain(Limits limits)
      : min_gain_db_(std::max(limits.min_gain_db.value_or(kMinGainDb), kMinGainDb)),
        max_gain_db_(std::min(limits.max_gain_db.value_or(kMaxGainDb), kMaxGainDb)),
        min_gain_scale_(DbToScale(min_gain_db_)),
        max_gain_scale_(DbToScale(max_gain_db_)),
        cached_combined_gain_scale_(std::clamp(kUnityScale, min_gain_scale_, max_gain_scale_)) {
    if constexpr (kLogGainScaleCalculation) {
      FX_LOGS(INFO) << "Gain(" << this << ") created with min_gain_scale_: " << min_gain_scale_
                    << ", max_gain_scale_: " << max_gain_scale_;
    }
  }

  // Retrieves the overall gain-scale, combining the Source and Dest controls.
  AScale GetGainScale();
  float GetGainDb() { return std::max(ScaleToDb(GetGainScale()), kMinGainDb); }

  // Calculates and returns an array of gain-scale values for the next `num_frames`. The returned
  // value is the maximum gain-scale value over that interval.
  AScale CalculateScaleArray(AScale* scale_arr, int64_t num_frames, const TimelineRate& rate);

  // Returns the current gain from each control, including mute effects.
  float GetSourceGainDb() const {
    return source_.IsMuted() ? kMinGainDb : std::clamp(source_.GainDb(), kMinGainDb, kMaxGainDb);
  }
  float GetDestGainDb() const {
    return dest_.IsMuted() ? kMinGainDb : std::clamp(dest_.GainDb(), kMinGainDb, kMaxGainDb);
  }

  // These functions determine which performance-optimized templatized functions we use for a Mix.
  // Thus they include knowledge about the foreseeable future (e.g. ramping).
  //
  // IsSilent:      Muted OR (current gain is silent AND not ramping toward >kMinGainDb).
  // IsUnity:       Current gain == kUnityGainDb AND not ramping.
  // IsRamping:     Remaining ramp duration > 0 AND not muted.
  //
  bool IsSilent() {
    return source_.IsMuted() || dest_.IsMuted() ||
           // source is currently silent and not ramping up
           (source_.GainDb() <= kMinGainDb && !source_.IsRampingUp()) ||
           // or dest is currently silent and not ramping up
           (dest_.GainDb() <= kMinGainDb && !dest_.IsRampingUp()) ||
           // or the combination is silent and neither is ramping up
           (source_.GainDb() + dest_.GainDb() <= kMinGainDb && !source_.IsRampingUp() &&
            !dest_.IsRampingUp());
  }

  bool IsUnity() {
    return !source_.IsMuted() && !source_.IsRamping() && !dest_.IsMuted() && !dest_.IsRamping() &&
           (source_.GainDb() + dest_.GainDb() == 0.0f) && (min_gain_db_ <= kUnityGainDb) &&
           (max_gain_db_ >= kUnityGainDb);
  }

  bool IsRamping() {
    return !source_.IsMuted() && !dest_.IsMuted() && (source_.IsRamping() || dest_.IsRamping());
  }

  // Manipulates the Source control.
  void SetSourceGain(float gain_db) { source_.SetGain(gain_db); }
  void SetSourceMute(bool mute) { source_.SetMute(mute); }

  void SetSourceGainWithRamp(
      float gain_db, zx::duration duration,
      fuchsia::media::audio::RampType ramp_type = fuchsia::media::audio::RampType::SCALE_LINEAR) {
    source_.SetGainWithRamp(gain_db, duration, ramp_type);
  }

  void CompleteSourceRamp() { source_.CompleteRamp(); }

  // Manipulates the Dest control.
  // The Dest control does not have a mute toggle.
  void SetDestGain(float gain_db) { dest_.SetGain(gain_db); }

  void SetDestGainWithRamp(
      float gain_db, zx::duration duration,
      fuchsia::media::audio::RampType ramp_type = fuchsia::media::audio::RampType::SCALE_LINEAR) {
    dest_.SetGainWithRamp(gain_db, duration, ramp_type);
  }

  void CompleteDestRamp() { dest_.CompleteRamp(); }

  // Advances the state of all in-progress ramps by the specified number of frames.
  void Advance(int64_t num_frames, const TimelineRate& rate) {
    source_.Advance(num_frames, rate);
    dest_.Advance(num_frames, rate);
  }

 private:
  const float min_gain_db_;
  const float max_gain_db_;
  const float min_gain_scale_;
  const float max_gain_scale_;

  // These three fields are used to memoize the result of GetGainScale.
  float cached_source_gain_db_ = kUnityGainDb;
  float cached_dest_gain_db_ = kUnityGainDb;
  AScale cached_combined_gain_scale_ = kUnityScale;

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
      if constexpr (kLogSetGain) {
        FX_LOGS(INFO) << "Gain(" << this << "): " << name_ << ".SetGain(" << gain_db
                      << "), was gain_db " << gain_db_ << ", start_db " << ramp_start_gain_db_
                      << ", end_db " << ramp_end_gain_db_;
      }
      ramp_duration_ = zx::nsec(0);
      gain_db_ = gain_db;
    }

    void SetMute(bool mute) {
      if constexpr (kLogSetMute) {
        FX_LOGS(INFO) << "Gain(" << this << "): " << name_ << ".SetMute(" << std::boolalpha << mute
                      << "), was " << mute_;
      }
      mute_ = mute;
    }

    void SetGainWithRamp(float gain_db, zx::duration duration,
                         fuchsia::media::audio::RampType ramp_type);

    void CompleteRamp() {
      if constexpr (kLogRampAdvance) {
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
    float gain_db_ = kUnityGainDb;
    bool mute_ = false;

    // A linear ramp from ramp_start_scale_ to ramp_end_scale_ over ramp_duration_.
    float ramp_start_scale_ = kUnityScale;
    float ramp_start_gain_db_ = kUnityGainDb;
    float ramp_end_scale_ = kUnityScale;
    float ramp_end_gain_db_ = kUnityGainDb;

    zx::duration ramp_duration_;        // if zero, we are not ramping
    int64_t frames_ramped_so_far_ = 0;  // how many frames ramped so far
  };

  Control source_{"source"};
  Control dest_{"dest"};
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_GAIN_H_
