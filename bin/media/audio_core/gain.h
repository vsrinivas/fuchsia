// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_GAIN_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_GAIN_H_

#include <atomic>

#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>

#include "garnet/bin/media/audio_core/constants.h"

namespace media {
namespace audio {

// A small class used to hold the representation of a factor used for software
// scaling of audio in the mixer pipeline.
class Gain {
 public:
  // Amplitude scale factors are expressed as 32-bit IEEE-754 floating point.
  using AScale = float;

  // constructor
  Gain() : db_target_rend_gain_(0.0f) {}

  // Audio gains for renderers/capturers and output devices are expressed as
  // floating-point values, in decibels. For each signal path, two gain values
  // are combined and then stored in the API-to-device link (usually
  // renderer-to-output), as a 32-bit floating-point amplitude multiplier.
  //
  // Examples: Renderer gain + Output gain = combined gain for a playback path.
  // Input device gain + Capturer gain = combined gain for an audio input path.
  static constexpr float kMinGainDb = fuchsia::media::MUTED_GAIN;
  static constexpr float kMaxGainDb = fuchsia::media::MAX_GAIN;

  static constexpr AScale kUnityScale = 1.0f;
  static constexpr AScale kMaxScale = 15.8489319f;  // kMaxGainDb is +24.0 dB
  static constexpr AScale kMinScale = 0.00000001f;  // kMinGainDb is -160.0 dB

  // TODO(mpuryear): MTWN-70 Clarify/document/test audio::Gain's thread-safety
  //
  // Set the renderer's contribution to a link's overall software gain
  // control. With a 4.28 fixed point internal amplitude scalar, we allow values
  // in the range of [-inf, 24.0]. Callers of this method must guarantee
  // single-threaded semantics for each Gain instance. This is guaranteed today
  // because only API-side components (not the mixer) call this method from
  // their execution domain (giving us the single-threaded guarantee).
  // This value is stored in an atomic float, so that the Mixer can consume it
  // at any time without our needing to use a lock for synchronization.
  void SetRendererGain(float db_gain) { db_target_rend_gain_.store(db_gain); }

  // Retrieve the combined amplitude scalar for this Gain, when provided a gain
  // value for the "destination" side of this link (output device, or audio
  // capturer API). This will only ever be called by the mixer or the single
  // capturer for this audio path. For performance reasons, values are cached
  // and the scalar recomputed only when needed.
  AScale GetGainScale(float output_db_gain);

  // Helper function which gives the value of the mute threshold for an
  // amplitude scale value, for any incoming sample format.
  //
  // @return The value at which the amplitude scaler is guaranteed to drive all
  // sample values to a value of 0 (meaning that we waste compute cycles if we
  // actually scale anything). Note: because we normalize all input formats to
  // the same full-scale bounds, this value is identical for all input types.
  // This gain_scale value takes rounding into effect in its calculation.
  static constexpr AScale MuteThreshold() { return kMinScale; }

 private:
  std::atomic<float> db_target_rend_gain_;
  float db_current_rend_gain_ = kMinGainDb;
  float db_current_output_gain_ = kMinGainDb;
  AScale combined_gain_scale_ = 0.0f;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_GAIN_H_
