// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace overnet {

// Number of microseconds per millisecond.
static const uint64_t kUsPerMs = 1000;
static const uint64_t kUsPerSec = 1000000;

class TimeDelta {
 public:
  static constexpr TimeDelta PositiveInf() {
    return TimeDelta(std::numeric_limits<int64_t>::max());
  }
  static constexpr TimeDelta NegativeInf() {
    return TimeDelta(std::numeric_limits<int64_t>::min());
  }
  static constexpr TimeDelta Zero() { return TimeDelta(0); }
  static constexpr TimeDelta FromMicroseconds(int64_t delta) { return TimeDelta(delta); }
  static constexpr TimeDelta FromMilliseconds(int64_t delta) { return TimeDelta(delta * kUsPerMs); }
  static constexpr TimeDelta FromSeconds(int64_t delta) { return TimeDelta(delta * kUsPerSec); }
  static constexpr TimeDelta FromMinutes(int64_t delta) { return FromSeconds(delta * 60); }
  static constexpr TimeDelta FromHours(int64_t delta) { return FromMinutes(delta * 60); }

  constexpr int64_t as_us() const { return delta_; }

 private:
  explicit constexpr TimeDelta(int64_t delta) : delta_(delta) {}
  int64_t delta_;
};

class TimeStamp {
 public:
  static constexpr TimeStamp Epoch() { return TimeStamp(TimeDelta::Zero()); }
  static constexpr TimeStamp AfterEpoch(TimeDelta td) { return TimeStamp(td); }

  constexpr TimeDelta after_epoch() const { return td_; }

 private:
  explicit constexpr TimeStamp(TimeDelta td) : td_(td) {}

  TimeDelta td_;
};

inline constexpr bool operator==(TimeDelta a, TimeDelta b) { return a.as_us() == b.as_us(); }

inline constexpr bool operator!=(TimeDelta a, TimeDelta b) { return a.as_us() != b.as_us(); }

inline std::ostream& operator<<(std::ostream& out, TimeDelta a) {
  if (a == TimeDelta::PositiveInf())
    return out << "+inf";
  if (a == TimeDelta::NegativeInf())
    return out << "-inf";
  auto us = a.as_us();
  if (us > -1000 && us < 1000)
    return out << us << "us";
  if (us > -1000000 && us < 1000000)
    return out << (us / 1000.0) << "ms";
  return out << (us / 1000000.0) << "s";
}

inline std::ostream& operator<<(std::ostream& out, TimeStamp ts) {
  return out << '@' << ts.after_epoch();
}

inline constexpr TimeDelta operator+(TimeDelta a, TimeDelta b) {
  if (a == TimeDelta::PositiveInf() || a == TimeDelta::NegativeInf() || b == TimeDelta::Zero()) {
    return a;
  }
  if (b == TimeDelta::PositiveInf())
    return b;
  if (b == TimeDelta::NegativeInf())
    return b;
  if (b.as_us() > 0) {
    if (a.as_us() > 0 && b.as_us() > TimeDelta::PositiveInf().as_us() - a.as_us()) {
      return TimeDelta::PositiveInf();
    }
  } else {
    if (a.as_us() < 0 && b.as_us() < TimeDelta::NegativeInf().as_us() - a.as_us()) {
      return TimeDelta::NegativeInf();
    }
  }
  return TimeDelta::FromMicroseconds(a.as_us() + b.as_us());
}

inline constexpr TimeDelta operator-(TimeDelta x) {
  if (x == TimeDelta::NegativeInf())
    return TimeDelta::PositiveInf();
  if (x == TimeDelta::PositiveInf())
    return TimeDelta::NegativeInf();
  return TimeDelta::FromMicroseconds(-x.as_us());
}

inline constexpr TimeDelta operator-(TimeDelta a, TimeDelta b) { return a + (-b); }

inline constexpr bool operator>(TimeDelta a, TimeDelta b) { return a.as_us() > b.as_us(); }

inline constexpr bool operator<(TimeDelta a, TimeDelta b) { return a.as_us() < b.as_us(); }

inline constexpr bool operator>=(TimeDelta a, TimeDelta b) { return a.as_us() >= b.as_us(); }

inline constexpr bool operator<=(TimeDelta a, TimeDelta b) { return a.as_us() <= b.as_us(); }

inline constexpr TimeDelta operator-(TimeStamp a, TimeStamp b) {
  return a.after_epoch() - b.after_epoch();
}

inline constexpr TimeStamp operator+(TimeStamp a, TimeDelta b) {
  return TimeStamp::AfterEpoch(a.after_epoch() + b);
}

inline constexpr TimeStamp operator-(TimeStamp a, TimeDelta b) {
  return TimeStamp::AfterEpoch(a.after_epoch() - b);
}

inline TimeStamp& operator+=(TimeStamp& a, TimeDelta b) {
  a = a + b;
  return a;
}

inline constexpr TimeDelta operator*(int multiplier, TimeDelta x) {
  if (multiplier == 0)
    return TimeDelta::Zero();
  bool neg = false;
  if (multiplier < 0) {
    neg = true;
    multiplier = -multiplier;
  }
  TimeDelta r = x;
  for (int i = 1; i < multiplier; i++) {
    r = r + x;
  }
  if (neg) {
    return -r;
  } else {
    return r;
  }
}

inline constexpr TimeDelta operator/(TimeDelta x, int divisor) {
  if (divisor == 0) {
    if (x.as_us() < 0) {
      return TimeDelta::NegativeInf();
    } else {
      return TimeDelta::PositiveInf();
    }
  }
  if (x == TimeDelta::NegativeInf()) {
    return TimeDelta::NegativeInf();
  }
  if (x == TimeDelta::PositiveInf()) {
    return TimeDelta::PositiveInf();
  }
  return TimeDelta::FromMicroseconds(x.as_us() / divisor);
}

inline constexpr bool operator>(TimeStamp a, TimeStamp b) {
  return a.after_epoch() > b.after_epoch();
}

inline constexpr bool operator<(TimeStamp a, TimeStamp b) {
  return a.after_epoch() < b.after_epoch();
}

inline constexpr bool operator>=(TimeStamp a, TimeStamp b) {
  return a.after_epoch() >= b.after_epoch();
}

inline constexpr bool operator<=(TimeStamp a, TimeStamp b) {
  return a.after_epoch() <= b.after_epoch();
}

inline constexpr bool operator==(TimeStamp a, TimeStamp b) {
  return a.after_epoch() == b.after_epoch();
}

inline constexpr bool operator!=(TimeStamp a, TimeStamp b) {
  return a.after_epoch() != b.after_epoch();
}

}  // namespace overnet
