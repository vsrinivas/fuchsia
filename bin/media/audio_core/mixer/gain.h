// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_GAIN_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_GAIN_H_

#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>
#include <atomic>

#include "garnet/bin/media/audio_core/mixer/constants.h"

namespace media {
namespace audio {

// A small class used to hold the representation of a factor used for software
// scaling of audio in the mixer pipeline.
class Gain {
 public:
  // Amplitude scale factors are expressed as 32-bit IEEE-754 floating point.
  using AScale = float;

  // constructor
  Gain() : target_src_gain_db_(0.0f), target_dest_gain_db_(0.0f) {}

  // Audio gains for AudioRenderers/AudioCapturers and output devices are
  // expressed as floating-point values, in decibels. For each signal path, two
  // gain values are combined and then stored in the API-to-device link (usually
  // AudioRenderer-to-output), as a 32-bit floating-point amplitude multiplier.
  //
  // Examples: renderer gain + Output gain = combined gain for a playback path.
  // Input device gain + audio in gain = combined gain for an audio input path.
  static constexpr float kMinGainDb = fuchsia::media::MUTED_GAIN_DB;
  static constexpr float kMaxGainDb = fuchsia::media::MAX_GAIN_DB;

  // Helper constant values in the gain-scale domain.
  //
  // kUnityScale is the scale value at which mix inputs are passed bit-for-bit
  // through the mixer into the accumulation buffer. This is used during the Mix
  // process as an optimization, to avoid unnecessary multiplications.
  //
  // kMaxScale is the scale value corresponding to the largest allowed gainDb
  // values, which is currently +24.0 decibels. Scale values above this value
  // will be clamped to this value.
  //
  // kMinScale is the value at which the amplitude scaler is guaranteed to drive
  // all sample values to a value of 0 (meaning that we waste compute cycles if
  // we actually scale anything). Note: because we normalize all input formats
  // to the same full-scale bounds, this value is identical for all input types.
  // This gain_scale value takes rounding into account in its calculation.
  static constexpr AScale kUnityScale = 1.0f;
  static constexpr AScale kMaxScale = 15.8489319f;  // kMaxGainDb is +24.0 dB
  static constexpr AScale kMinScale = 0.00000001f;  // kMinGainDb is -160.0 dB

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
  void SetSourceGain(float gain_db) { target_src_gain_db_.store(gain_db); }

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
  // when performing future Mix operations for that particular source.
  void SetDestGain(float gain_db) { target_dest_gain_db_.store(gain_db); }

  // Retrieve the combined amplitude scale for this Gain, when provided the
  // "destination" gain (output device, or capturer in API). This is only
  // called by the mixer for this audio path. For performance reasons, values
  // are cached and recomputed only as needed.
  Gain::AScale GetGainScale(float dest_gain_db) {
    return GetGainScale(target_src_gain_db_.load(), dest_gain_db);
  }

  // Calculate the stream's gain-scale, from cached source and dest values.
  Gain::AScale GetGainScale() {
    return GetGainScale(target_src_gain_db_.load(),
                        target_dest_gain_db_.load());
  }

  // Convenience functions to aid in performance optimization.
  // NOTE: These methods expect the caller to use SetDestGain, NOT the
  // GetGainScale(dest_gain_db) variant -- it doesn't cache dest_gain_db.
  bool IsUnity() { return (GetGainScale() == kUnityScale); }
  bool IsSilent() { return (GetGainScale() == 0.0f); }

 private:
  // Called by the above GetGainScale variants. For performance reasons, this
  // implementation caches values and recomputes the result only as needed.
  Gain::AScale GetGainScale(float src_gain_db, float dest_gain_db);

  // TODO(mpuryear): at some point, we should examine whether using these two
  // atomics gives better performance and scale than using a lock instead.
  std::atomic<float> target_src_gain_db_;
  std::atomic<float> target_dest_gain_db_;

  float current_src_gain_db_ = 0.0f;
  float current_dest_gain_db_ = 0.0f;
  AScale combined_gain_scale_ = kUnityScale;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_GAIN_H_
