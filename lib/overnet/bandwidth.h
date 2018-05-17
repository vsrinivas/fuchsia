// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ostream>
#include "timer.h"

namespace overnet {

class Bandwidth {
 public:
  constexpr static Bandwidth Zero() { return Bandwidth(0); }

  constexpr static Bandwidth FromBitsPerSecond(uint64_t bits_per_second) {
    return Bandwidth(bits_per_second);
  }

  constexpr static Bandwidth BytesPerTime(uint64_t bytes, TimeDelta delta) {
    return FromBitsPerSecond(8000000 * bytes / delta.as_us());
  }

  constexpr static Bandwidth FromKilobitsPerSecond(
      uint64_t kilobits_per_second) {
    return FromBitsPerSecond(kilobits_per_second * 1000);
  }

  constexpr TimeDelta SendTimeForBytes(uint64_t bytes) const {
    return TimeDelta::FromMicroseconds(8000000 * bytes / bits_per_second_);
  }

  constexpr uint64_t BytesSentForTime(TimeDelta time) const {
    return bits_per_second_ * time.as_us() / 8000000;
  }

  constexpr uint64_t bits_per_second() const { return bits_per_second_; }

 private:
  constexpr Bandwidth(uint64_t bits_per_second)
      : bits_per_second_(bits_per_second) {}

  uint64_t bits_per_second_;
};

inline bool operator>(Bandwidth a, Bandwidth b) {
  return a.bits_per_second() > b.bits_per_second();
}

inline bool operator>=(Bandwidth a, Bandwidth b) {
  return a.bits_per_second() >= b.bits_per_second();
}

inline bool operator<(Bandwidth a, Bandwidth b) {
  return a.bits_per_second() < b.bits_per_second();
}

inline bool operator<=(Bandwidth a, Bandwidth b) {
  return a.bits_per_second() <= b.bits_per_second();
}

inline bool operator==(Bandwidth a, Bandwidth b) {
  return a.bits_per_second() == b.bits_per_second();
}

inline std::ostream& operator<<(std::ostream& out, Bandwidth b) {
  uint64_t bits_per_second = b.bits_per_second();
  if (bits_per_second < 1000) {
    out << bits_per_second << "bps";
  } else if (bits_per_second < 1000000) {
    out << (bits_per_second / 1000.0) << "Kbps";
  } else {
    out << (bits_per_second / 1000000.0) << "Mbps";
  }
  return out;
}

}  // namespace overnet
