// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio/level.h"

#include <cmath>
#include <limits>

#include "apps/media/src/audio/gain.h"

namespace media {

// Level<float> template specializations.

template <>
// static
const Level<float> Level<float>::Silence = Level(0.0f);

template <>
// static
const Level<float> Level<float>::Unity = Level(1.0f);

template <>
// static
Level<float> Level<float>::FromGain(Gain gain) {
  // Highest silent gain was determined using the canonical formula below and
  // a binary search. This value is duplicated in the unit test, and the two
  // should be kept in sync. We use this value to avoid using the canonical
  // formula. It's important that the canonical formula applied to this value
  // yields 0.0f. It's OK if a small range of higher gain values also do, but
  // we want this number to be as high as possible while still meeting the
  // constraint.
  static constexpr float kHighestSilentGain = -451.545f;

  // Sufficiently low gain values should produce Silence.
  if (gain.value() <= kHighestSilentGain) {
    return Silence;
  }

  // Gain::Unity should produce Unity.
  if (gain == Gain::Unity) {
    return Unity;
  }

  // Use the canonical formula.
  return Level(std::pow(10.0f, gain.value() / 10.0f));
}

template <>
Gain Level<float>::ToGain() const {
  // Unity should produce Gain::Unity.
  if (*this == Unity) {
    return Gain::Unity;
  }

  // Silence should produce Gain::Silence.
  if (*this == Silence) {
    return Gain::Silence;
  }

  // Use the canonical formula.
  return Gain(10.0f * std::log10(value_));
}

}  // namespace media
