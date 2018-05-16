// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <atomic>

#include <media/cpp/fidl.h>

#include "garnet/bin/media/audio_server/constants.h"

namespace media {
namespace audio {

// A small class used to hold the representation of a factor used for software
// scaling of audio in the mixer pipeline.
class Gain {
 public:
  // Amplitude scale factors are currently expressed as 4.28 fixed point
  // integers stores in unsigned 32 bit integers.
  using AScale = uint32_t;

  // constructor
  Gain() : db_target_rend_gain_(0.0f) {}

  // Audio gains for renderers/capturers and output devices are expressed as
  // floating-point values, in decibels. For each signal path, two gain values
  // are combined and then stored in the API-to-device link (usually
  // renderer-to-output), as a 4.28 fixed-point amplitude scale factor.
  //
  // Examples: Renderer gain + Output gain = combined gain for a playback path.
  // Input device gain + Capturer gain = combined gain for an audio input path.
  static constexpr unsigned int kFractionalScaleBits = 28;
  // Used to add 'rounding' to 4.28 samples, before shift-down (truncation).
  static constexpr unsigned int kFractionalRoundValue =
      (1u) << (kFractionalScaleBits - 1);
  static constexpr AScale kUnityScale =
      (static_cast<AScale>(1u) << kFractionalScaleBits);
  static constexpr AScale kMaxScale = 0xFD9539A4;  // +24.0 dB: kMaxGain

  static constexpr float kMinGain = media::kMutedGain;
  static constexpr float kMaxGain = media::kMaxGain;

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
  Gain::AScale GetGainScale(float output_db_gain);

  // Helper function which gives the value of the mute threshold for an
  // amplitude scale value, for any incoming sample format.
  //
  // @return The value at which the amplitude scaler is guaranteed to drive all
  // sample values to a value of 0 (meaning that we waste compute cycles if we
  // actually scale anything). Note: because we normalize all input formats to
  // the same full-scale bounds, this value is identical for all input types.
  // This gain_scale value takes rounding into effect in its calculation.
  static constexpr AScale MuteThreshold() {
    return (static_cast<AScale>(1u)
            << (kFractionalScaleBits - kAudioPipelineWidth)) -
           1;
  }

 private:
  std::atomic<float> db_target_rend_gain_;
  float db_current_rend_gain_ = kMinGain;
  float db_current_output_gain_ = kMinGain;
  AScale amplitude_scale_ = 0u;
};

}  // namespace audio
}  // namespace media
