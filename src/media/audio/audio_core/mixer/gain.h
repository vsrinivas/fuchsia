// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_GAIN_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_GAIN_H_

#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>

#include <atomic>
#include <cmath>

#include "lib/media/timeline/timeline_rate.h"
#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/mixer/constants.h"

namespace media::audio {

constexpr bool kVerboseGainDebug = false;
constexpr bool kVerboseMuteDebug = false;
constexpr bool kVerboseRampDebug = false;

// A class containing factors used for software scaling in the mixer pipeline.
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

  // constructor
  Gain()
      : target_src_gain_db_(kUnityGainDb),
        target_dest_gain_db_(kUnityGainDb),
        source_ramp_duration_ns_(0),
        frames_ramped_(0) {}

  // Amplitude scale factors are expressed as 32-bit IEEE-754 floating point.
  using AScale = float;

  // Note: multiply-by-.05 equals divide-by-20 -- and is faster on non-optimized
  // builds. Note: 0.05 must be double (not float), for the precision we
  // require.
  static AScale DbToScale(float gain_db) { return pow(10.0f, gain_db * 0.05); }
  static float ScaleToDb(AScale scale) { return std::log10(scale) * 20.0f; }
  // Higher-precision (but slower) version currently used only by fidelity tests
  static double DoubleToDb(double val) { return std::log10(val) * 20.0; }

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

  // TODO(mpuryear): MTWN-70 Clarify/document/test audio::Gain's thread-safety
  //
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
  // These SetGain calls set the source's or destination's contribution to a
  // link's overall software gain control. For stream gain, we allow values in
  // the range [-inf, 24.0]. Callers must guarantee single-threaded semantics
  // for each Gain instance. This is guaranteed today because only API-side
  // components (not mixer) call this from their execution domain (guaranteeing
  // single-threadedness). This value is stored in atomic float -- the Mixer can
  // consume it at any time without needing a lock for synchronization.
  void SetSourceGain(float gain_db) {
    target_src_gain_db_.store(gain_db);
    if constexpr (kVerboseGainDebug) {
      FXL_LOG(INFO) << "Gain(" << this << "): SetSourceGain(" << gain_db << ")";
    }
  }

  void SetSourceMute(bool muted) {
    src_mute_ = muted;
    if constexpr (kVerboseMuteDebug) {
      FXL_LOG(INFO) << "Gain(" << this << "): SetSourceMute(" << muted << ")";
    }
  }

  // Smoothly change the source gain over the specified period of playback time.
  void SetSourceGainWithRamp(float gain_db, zx_duration_t duration_ns,
                             fuchsia::media::audio::RampType ramp_type =
                                 fuchsia::media::audio::RampType::SCALE_LINEAR);

  void ClearSourceRamp() { source_ramp_duration_ns_ = 0; }

  // The atomics for target_src_gain_db and target_dest_gain_db are meant to
  // defend a Mix thread's gain READs, against gain WRITEs by another thread in
  // response to SetGain calls. For playback, this generally always means writes
  // of the SOURCE gain (for capture, generally this means DEST gain changes --
  // either way we are talking about changes to the Stream's gain). DEST gain is
  // provided to Gain objects, but those objects don't own this setting. Gain
  // objects correspond to stream mixes, so they are 1-1 with source gains;
  // however, there are many stream mixes for a single destination -- thus many
  // gain objects share the same destination (share the same dest gain). So,
  // gain objects don't contain the definitive value of any dest gain.

  // The DEST gain "written" to a Gain object is just a snapshot of the dest
  // gain held by the audio_capturer_impl or output device. We use this snapshot
  // when performing the current Mix operation for that particular source.
  void SetDestGain(float gain_db) {
    target_dest_gain_db_.store(gain_db);
    if constexpr (kVerboseGainDebug) {
      FXL_LOG(INFO) << "Gain(" << this << "): SetDestGain(" << gain_db << ")";
    }
  }

  void SetDestMute(bool muted) {
    dest_mute_ = muted;
    if constexpr (kVerboseMuteDebug) {
      FXL_LOG(INFO) << "Gain(" << this << "): SetDestMute(" << muted << ")";
    }
  }

  // Calculate the stream's gain-scale, from cached source and dest values.
  AScale GetGainScale() {
    return GetGainScale(target_src_gain_db_.load(),
                        target_dest_gain_db_.load());
  }

  // Retrieve combined amplitude scale for a mix stream, given gain for a mix's
  // "destination" (output device, or capturer in API). Only called by a link's
  // mixer. For performance, values are cached and recomputed only as needed.
  AScale GetGainScale(float dest_gain_db) {
    return GetGainScale(target_src_gain_db_.load(), dest_gain_db);
  }

  void GetScaleArray(AScale* scale_arr, uint32_t num_frames,
                     const TimelineRate& rate);

  // Advance the state of any gain ramp by the specified number of frames.
  void Advance(uint32_t num_frames, const TimelineRate& rate);

  // Convenience functions to aid in performance optimization.
  // NOTE: These methods expect the caller to use SetDestGain, NOT the
  // GetGainScale(dest_gain_db) variant -- it doesn't cache dest_gain_db.
  bool IsUnity() {
    return (target_src_gain_db_.load() == -(target_dest_gain_db_.load())) &&
           !src_mute_ && !dest_mute_ && !IsRamping();
  }

  bool IsSilent() {
    return src_mute_ || dest_mute_ ||
           (IsSilentNow() &&
            (!IsRamping() || start_src_gain_db_ >= end_src_gain_db_ ||
             end_src_gain_db_ <= kMinGainDb));
  }

  // Note: a Gain object can be considered "ramping" even if it is Muted.
  bool IsRamping() { return (source_ramp_duration_ns_ > 0); }

 private:
  // Called by the above GetGainScale variants. For performance reasons, this
  // implementation caches values and recomputes the result only as needed.
  AScale GetGainScale(float src_gain_db, float dest_gain_db);

  // Used internally only -- the instananeous gain state
  bool IsSilentNow() {
    return (target_src_gain_db_.load() <= kMinGainDb) ||
           (target_dest_gain_db_.load() <= kMinGainDb) ||
           (target_src_gain_db_.load() + target_dest_gain_db_.load() <=
            kMinGainDb);
  }

  // TODO(mpuryear): at some point, examine whether using a lock provides better
  // performance and scalability than using these two atomics.
  std::atomic<float> target_src_gain_db_;
  std::atomic<float> target_dest_gain_db_;

  float current_src_gain_db_ = kUnityGainDb;
  bool src_mute_ = false;
  float current_dest_gain_db_ = kUnityGainDb;
  bool dest_mute_ = false;
  AScale combined_gain_scale_ = kUnityScale;

  float start_src_scale_ = kUnityScale;
  float start_src_gain_db_ = kUnityGainDb;
  float end_src_scale_ = kUnityScale;
  float end_src_gain_db_ = kUnityGainDb;
  zx_duration_t source_ramp_duration_ns_;
  uint32_t frames_ramped_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_GAIN_H_
