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
  static float ScaleToDb(AScale scale) { return std::log10(scale) * 20.0f; }
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
        combined_gain_scale_(std::clamp(kUnityScale, min_gain_scale_, max_gain_scale_)) {}

  // The Gain object specifies the volume scaling to be performed for a given
  // Mix operation, when mixing a single stream into some combined resultant
  // audio stream. Restated, a Mix has one or more Sources, and it combines
  // these Sources to get a single stream for that Mix's Destination.
  // Correspondingly, Gain objects relate one-to-one with Source streams and
  // share a Destination stream with all other Source streams in that mix.
  // During playback, the renderer stream is the Source, and the output device
  // is the Destination. During capture, the input device is the Source, and the
  // capturer stream is the Destination (emitted via API to app clients).
  //
  // Retrieve the overall gain-scale, recalculating from respective pieces if needed.
  AScale GetGainScale();

  // Calculate and return an array of gain-scale values for the next `num_frames`. The returned
  // value is the maximum gain-scale value over that interval.
  AScale GetScaleArray(AScale* scale_arr, int64_t num_frames, const TimelineRate& rate);

  // Calculate the gain-scale, then convert it to decibels-full-scale.
  float GetGainDb() { return ScaleToDb(GetGainScale()); }

  // Return the partial components of a stream's gain_db, including mute effects. Note that this
  // uses the latest source or dest gain_db values that have been set, but it does not automatically
  // recalculate gain-scales, as these are not needed in order to return this DB value.
  float GetSourceGainDb() const {
    return source_mute_ ? kMinGainDb : std::clamp(target_source_gain_db_, kMinGainDb, kMaxGainDb);
  }
  float GetDestGainDb() const { return std::clamp(target_dest_gain_db_, kMinGainDb, kMaxGainDb); }

  // These functions determine which performance-optimized templatized functions we use for a Mix.
  // Thus they include knowledge about the foreseeable future (e.g. ramping).
  //
  // IsSilent:      Muted OR (current gain is silent AND not ramping toward >kMinGainDb).
  // IsUnity:       Current gain == kUnityGainDb AND not ramping.
  // IsRamping:     Remaining ramp duration > 0 AND not muted.
  //
  bool IsSilent() {
    // Not only currently silent, but also either
    return IsSilentNow() &&
           // ... source gain causes silence (is below mute point), regardless of dest gain, or
           ((target_source_gain_db_ <= kMinGainDb && source_ramp_duration_.get() == 0) ||
            // .. dest gain causes silence (is below mute point), regardless of source gain, or
            (target_dest_gain_db_ <= kMinGainDb && dest_ramp_duration_.get() == 0) ||
            ((source_ramp_duration_.get() == 0 || start_source_gain_db_ >= end_source_gain_db_) &&
             // ... all stages that are ramping must be downward.
             (dest_ramp_duration_.get() == 0 || start_dest_gain_db_ >= end_dest_gain_db_)));
  }

  bool IsUnity() {
    return !IsMute() && !IsRamping() && (target_source_gain_db_ + target_dest_gain_db_ == 0.0f) &&
           (min_gain_db_ <= kUnityGainDb) && (max_gain_db_ >= kUnityGainDb);
  }

  bool IsRamping() {
    return !IsMute() && (source_ramp_duration_.get() > 0 || dest_ramp_duration_.get() > 0);
  }

  // These SetGain calls set the source's or destination's contribution to a
  // link's overall software gain control. For stream gain, we allow values in
  // the range [-inf, 24.0]. Callers must guarantee single-threaded semantics
  // for each Gain instance.
  void SetSourceGain(float gain_db) {
    if constexpr (kLogSetGain) {
      FX_LOGS(INFO) << "Gain(" << this << "): SetSourceGain(" << gain_db << "), was tgt_src_db "
                    << target_source_gain_db_ << ", start_src_db " << start_source_gain_db_
                    << ", end_src_db " << end_source_gain_db_ << ", tgt_dst_db "
                    << target_dest_gain_db_;
    }
    source_ramp_duration_ = zx::nsec(0);
    target_source_gain_db_ = gain_db;
  }
  void SetSourceMute(bool mute) {
    if constexpr (kLogSetMute) {
      FX_LOGS(INFO) << "Gain(" << this << "): SetSourceMute(" << (mute ? "TRUE" : "FALSE")
                    << "), was " << (source_mute_ ? "TRUE" : "FALSE");
    }
    source_mute_ = mute;
  }

  // Smoothly change the source gain over the specified period of playback time.
  void SetSourceGainWithRamp(
      float gain_db, zx::duration duration,
      fuchsia::media::audio::RampType ramp_type = fuchsia::media::audio::RampType::SCALE_LINEAR);

  // Stop ramping the source gain: advance immediately to the final source gain.
  void CompleteSourceRamp() {
    if constexpr (kLogRampAdvance) {
      FX_LOGS(INFO) << "Gain(" << this << "): " << __func__;
    }
    if (source_ramp_duration_ > zx::nsec(0)) {
      source_ramp_duration_ = zx::nsec(0);
      SetSourceGain(end_source_gain_db_);
    }
  }

  // DEST gain is provided to Gain objects, but those objects don't own this setting.
  // Gain objects correspond to stream mixes, so they are 1-1 with source gains;
  // however, there are many stream mixes for a single destination -- thus many
  // gain objects share the same destination (share the same dest gain). So,
  // gain objects don't contain the definitive value of any dest gain.
  //
  // The DEST gain "written" to a Gain object is just a snapshot of the dest
  // gain held by the audio_capturer_impl or output device. We use this snapshot
  // when performing the current Mix operation for that particular source.
  void SetDestGain(float gain_db) {
    if constexpr (kLogSetGain) {
      FX_LOGS(INFO) << "Gain(" << this << "): SetDestGain(" << gain_db << "), was tgt_dst_db "
                    << target_dest_gain_db_ << ", start_dst_db " << start_dest_gain_db_
                    << ", end_dst_db " << end_dest_gain_db_ << ", tgt_src_db "
                    << target_source_gain_db_;
    }
    dest_ramp_duration_ = zx::nsec(0);
    target_dest_gain_db_ = gain_db;
  }

  // Smoothly change the dest gain over the specified period of playback time.
  void SetDestGainWithRamp(
      float gain_db, zx::duration duration,
      fuchsia::media::audio::RampType ramp_type = fuchsia::media::audio::RampType::SCALE_LINEAR);

  // Stop ramping the dest gain: advance immediately to the final dest gain.
  void CompleteDestRamp() {
    if constexpr (kLogRampAdvance) {
      FX_LOGS(INFO) << __func__;
    }
    if (dest_ramp_duration_ > zx::nsec(0)) {
      dest_ramp_duration_ = zx::nsec(0);
      SetDestGain(end_dest_gain_db_);
    }
  }

  // Advance the state of any gain ramp by the specified number of frames.
  void Advance(int64_t num_frames, const TimelineRate& rate);

 private:
  // Internal functions for querying the current state of the Gain object
  //
  // Object is muted and will remain silent, regardless of gain or ramp values
  bool IsMute() const { return source_mute_; }

  // CURRENT gain <= kMinGainDb, including mute effects.
  bool IsSilentNow() const {
    return IsMute() || (target_source_gain_db_ <= kMinGainDb) ||
           (target_dest_gain_db_ <= kMinGainDb) ||
           (target_source_gain_db_ + target_dest_gain_db_ <= kMinGainDb);
  }

  // Recalculate the stream's gain-scale, from respective source and dest values that may have
  // changed since the last time this was called.
  void RecalculateGainScale();

  const float min_gain_db_;
  const float max_gain_db_;
  const float min_gain_scale_;
  const float max_gain_scale_;

  float target_source_gain_db_ = kUnityGainDb;
  float target_dest_gain_db_ = kUnityGainDb;

  bool source_mute_ = false;

  float current_source_gain_db_ = kUnityGainDb;
  float current_dest_gain_db_ = kUnityGainDb;
  AScale combined_gain_scale_ = kUnityScale;

  float start_source_scale_ = kUnityScale;
  float start_dest_scale_ = kUnityScale;
  float start_source_gain_db_ = kUnityGainDb;
  float start_dest_gain_db_ = kUnityGainDb;

  float end_source_scale_ = kUnityScale;
  float end_dest_scale_ = kUnityScale;
  float end_source_gain_db_ = kUnityGainDb;
  float end_dest_gain_db_ = kUnityGainDb;

  zx::duration source_ramp_duration_;
  zx::duration dest_ramp_duration_;
  int64_t source_frames_ramped_ = 0;
  int64_t dest_frames_ramped_ = 0;

  // Ultimately we will split source+dest portions, each in its own separate GainStage object which
  // can be individually controlled. The mixer will have access to a container CombinedGain or
  // GainSequence object that can only be read, reset and advanced.
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_GAIN_H_
