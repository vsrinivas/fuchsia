// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/audio/gain.h"

namespace mojo {
namespace media {

// Represents a linear audio level with underlying type T.
//
// This audio volume representation is intended for high-performance signal
// processing and won't be exposed in higher-level APIs. Levels are linear, so
// applying them to samples is just a multiply. Level is a template, because
// different underlying types are appropriate for different sample types. For
// float samples, Level<float> makes the most sense. For integer sample types
// such as int16_t and int32_t, a fixed-point Level based on an unsigned
// integer type is appropriate.
template <typename T>
struct Level {
 public:
  // Level that produces silence.
  static const Level Silence;

  // Level that leaves audio unmodified.
  static const Level Unity;

  // Produces a level value from a gain value.
  static Level FromGain(Gain gain);

  // Constructs a silent level.
  Level();

  // Construct a level from the specified underlying value.
  explicit Level(T value) : value_(value) {}

  // Returns the underlying value of the level.
  T value() const { return value_; }

  // Produces a gain value from this level value.
  Gain ToGain() const;

  bool operator==(const Level& other) const { return value_ == other.value_; }
  bool operator!=(const Level& other) const { return value_ != other.value_; }
  bool operator<(const Level& other) const { return value_ < other.value_; }
  bool operator>(const Level& other) const { return value_ > other.value_; }
  bool operator<=(const Level& other) const { return value_ <= other.value_; }
  bool operator>=(const Level& other) const { return value_ >= other.value_; }

 private:
  T value_;
};

// Level<float> template specializations.

template <>
// static
const Level<float> Level<float>::Silence;

template <>
// static
const Level<float> Level<float>::Unity;

template <>
// static
Level<float> Level<float>::FromGain(Gain gain);

template <>
Gain Level<float>::ToGain() const;

// Out-of-line constructor.

template <typename T>
Level<T>::Level() : value_(Silence.value()) {}

}  // namespace media
}  // namespace mojo
