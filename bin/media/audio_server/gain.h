// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <atomic>

#pragma once

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
  Gain();

  // Audio gains for tracks and outputs are expressed as floating point in
  // decibels.  Track and output gains are combined and the stored in the track
  // to output link as a 4.28 fixed point amplitude scale factor.
  static constexpr unsigned int FRACTIONAL_BITS = 28;
  static constexpr AScale UNITY = (static_cast<AScale>(1u) << FRACTIONAL_BITS);

  // Set the internal value of the amplitude scaler based on the dB value
  // passed.  With a 4.28 fixed point internal amplitude scalar, legal values
  // are on the range from [-inf, 24.0]
  void Set(float db_gain);

  // Force the internal amplitude scaler to represent infinite dB down.
  void ForceMute() { amplitude_scale_.store(0); }

  // Accessor for the current value of the amplitude scaler.
  AScale amplitude_scale() const { return amplitude_scale_.load(); }

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
    return (static_cast<AScale>(1u) << (FRACTIONAL_BITS - bit_count)) - 1;
  }

 private:
  std::atomic<AScale> amplitude_scale_;
};

}  // namespace audio
}  // namespace media
