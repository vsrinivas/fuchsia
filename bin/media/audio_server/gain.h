// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <stdint.h>

#include "apps/media/services/audio_renderer.fidl.h"

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
  Gain() : db_target_rend_gain_(0.0f) { }

  // Audio gains for renderers and outputs are expressed as floating point in
  // decibels.  Renderer and output gains are combined and the stored in the
  // renderer to output link as a 4.28 fixed point amplitude scale factor.
  static constexpr unsigned int kFractionalScaleBits = 28;
  static constexpr AScale kUnityScale =
    (static_cast<AScale>(1u) << kFractionalScaleBits);
  static constexpr float kMinGain = AudioRenderer::kMutedGain;
  static constexpr float kMaxGain = 24.0f;

  // Set the renderer's contribution to a link's overall software gain control.
  // With a 4.28 fixed point internal amplitude scalar, legal values are on the
  // range from [-inf, 24.0].  Safe to call from any thread, but should only
  // really be called from the main message loop thread.
  void SetRendererGain(float db_gain) { db_target_rend_gain_.store(db_gain); }

  // Get the current gain's amplitude scalar, given the current audio output's
  // gain value (recomputing only when needed).  Should only be called from the
  // AudioOutput's mixer thread.
  Gain::AScale GetGainScale(float output_db_gain);

  // Helper function which gives the value of the mute threshold for an
  // amplitude scale value for a sample with a given resolution.
  //
  // @param bit_count The number of non-sign bits for the signed integer
  // representation of the sample to be scaled.  For example, the bit count used
  // to compute the mute threshold when scaling 16 bit signed integers is 15.
  //
  // @return The value at which the amplitude scaler is guaranteed to drive all
  // samples to a value of 0 (meaning that you are wasting compute cycles
  // attempting to scale anything).  For example, when scaling 16 bit signed
  // integers, if amplitude_scale() <= MuteThreshold(15), then all scaled values
  // are going to end up as 0, so there is no point in performing the fixed
  // point multiply.
  static constexpr AScale MuteThreshold(unsigned int bit_count) {
    return (static_cast<AScale>(1u) << (kFractionalScaleBits - bit_count)) - 1;
  }

 private:
  std::atomic<float> db_target_rend_gain_;
  float db_current_rend_gain_ = kMinGain;
  float db_current_output_gain_ = kMinGain;
  AScale amplitude_scale_ = 0u;
};

}  // namespace audio
}  // namespace media
