// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits>

namespace media {

// Represents relative volume in logarithmic decibel units. Given two audio
// volumes a and b, the relative gain between a and b is 10 * log10(a / b).
struct Gain {
 public:
  // Relative gain between an audible volume and effective silence (i.e.
  // negative infinity gain).
  static const Gain Silence;

  // Relative gain between two equal volumes.
  static const Gain Unity;

  // Constructs a silent (negative infinity) Gain.
  Gain() : value_(Silence.value()) {}

  // Construct a Gain from the specified value.
  explicit Gain(float value) : value_(value) {}

  // Returns the underlying value of the level.
  float value() const { return value_; }

  bool operator==(const Gain& other) const { return value_ == other.value_; }
  bool operator!=(const Gain& other) const { return value_ != other.value_; }
  bool operator<(const Gain& other) const { return value_ < other.value_; }
  bool operator>(const Gain& other) const { return value_ > other.value_; }
  bool operator<=(const Gain& other) const { return value_ <= other.value_; }
  bool operator>=(const Gain& other) const { return value_ >= other.value_; }

 private:
  float value_;
};

}  // namespace media
