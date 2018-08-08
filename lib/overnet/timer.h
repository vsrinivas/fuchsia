// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <limits>
#include <ostream>
#include <type_traits>
#include "callback.h"
#include "optional.h"

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
  static constexpr TimeDelta FromMicroseconds(int64_t delta) {
    return TimeDelta(delta);
  }
  static constexpr TimeDelta FromMilliseconds(int64_t delta) {
    return TimeDelta(delta * kUsPerMs);
  }
  static constexpr TimeDelta FromSeconds(int64_t delta) {
    return TimeDelta(delta * kUsPerSec);
  }
  static constexpr TimeDelta FromMinutes(int64_t delta) {
    return FromSeconds(delta * 60);
  }
  static constexpr TimeDelta FromHours(int64_t delta) {
    return FromMinutes(delta * 60);
  }

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

inline constexpr bool operator==(TimeDelta a, TimeDelta b) {
  return a.as_us() == b.as_us();
}

inline constexpr bool operator!=(TimeDelta a, TimeDelta b) {
  return a.as_us() != b.as_us();
}

inline std::ostream& operator<<(std::ostream& out, TimeDelta a) {
  if (a == TimeDelta::PositiveInf()) return out << "+inf";
  if (a == TimeDelta::NegativeInf()) return out << "-inf";
  auto us = a.as_us();
  if (us > -1000 && us < 1000) return out << us << "us";
  if (us > -1000000 && us < 1000000) return out << (us / 1000.0) << "ms";
  return out << (us / 1000000.0) << "s";
}

inline std::ostream& operator<<(std::ostream& out, TimeStamp ts) {
  return out << '@' << ts.after_epoch();
}

inline constexpr TimeDelta operator+(TimeDelta a, TimeDelta b) {
  if (a == TimeDelta::PositiveInf() || a == TimeDelta::NegativeInf() ||
      b == TimeDelta::Zero()) {
    return a;
  }
  if (b == TimeDelta::PositiveInf()) return b;
  if (b == TimeDelta::NegativeInf()) return b;
  if (b.as_us() > 0) {
    if (a.as_us() > 0 &&
        b.as_us() > TimeDelta::PositiveInf().as_us() - a.as_us()) {
      return TimeDelta::PositiveInf();
    }
  } else {
    if (a.as_us() < 0 &&
        b.as_us() < TimeDelta::NegativeInf().as_us() - a.as_us()) {
      return TimeDelta::NegativeInf();
    }
  }
  return TimeDelta::FromMicroseconds(a.as_us() + b.as_us());
}

inline constexpr TimeDelta operator-(TimeDelta x) {
  if (x == TimeDelta::NegativeInf()) return TimeDelta::PositiveInf();
  if (x == TimeDelta::PositiveInf()) return TimeDelta::NegativeInf();
  return TimeDelta::FromMicroseconds(-x.as_us());
}

inline constexpr TimeDelta operator-(TimeDelta a, TimeDelta b) {
  return a + (-b);
}

inline constexpr bool operator>(TimeDelta a, TimeDelta b) {
  return a.as_us() > b.as_us();
}

inline constexpr bool operator<(TimeDelta a, TimeDelta b) {
  return a.as_us() < b.as_us();
}

inline constexpr bool operator>=(TimeDelta a, TimeDelta b) {
  return a.as_us() >= b.as_us();
}

inline constexpr bool operator<=(TimeDelta a, TimeDelta b) {
  return a.as_us() <= b.as_us();
}

inline constexpr TimeDelta operator-(TimeStamp a, TimeStamp b) {
  return a.after_epoch() - b.after_epoch();
}

inline constexpr TimeStamp operator+(TimeStamp a, TimeDelta b) {
  return TimeStamp::AfterEpoch(a.after_epoch() + b);
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

class Timeout;

class Timer {
  friend class Timeout;

 public:
  virtual TimeStamp Now() = 0;

  template <class F>
  void At(TimeStamp t, F f);

 protected:
  template <class T>
  T* TimeoutStorage(Timeout* timeout);

  static void FireTimeout(Timeout* timeout, Status status);

 private:
  virtual void InitTimeout(Timeout* timeout, TimeStamp when) = 0;
  virtual void CancelTimeout(Timeout* timeout, Status status) = 0;
};

class Timeout {
  friend class Timer;

 public:
  static constexpr uint64_t kMaxTimerStorage = 5 * sizeof(void*);

  Timeout(const Timeout&) = delete;
  Timeout& operator=(const Timeout&) = delete;

  // Initialize a timeout for timestamp when. cb will be called when the timeout
  // expires (with status OK) or when the timeout is cancelled (with non-OK
  // status).
  Timeout(Timer* timer, TimeStamp when, StatusCallback cb)
      : timer_(timer), cb_(std::move(cb)) {
    assert(!cb_.empty());
    timer_->InitTimeout(this, when);
  }

  ~Timeout() {
    if (!cb_.empty()) Cancel();
    assert(cb_.empty());
  }

  void Cancel(Status status = Status::Cancelled()) {
    timer_->CancelTimeout(this, std::move(status));
  }

 private:
  Timer* const timer_;
  StatusCallback cb_;
  typename std::aligned_storage<kMaxTimerStorage>::type storage_;
};

template <class T>
T* Timer::TimeoutStorage(Timeout* timeout) {
  static_assert(Timeout::kMaxTimerStorage >= sizeof(T),
                "Must have enough storage in Timeout for Timer data");
  return reinterpret_cast<T*>(&timeout->storage_);
}

inline void Timer::FireTimeout(Timeout* timeout, Status status) {
  if (timeout->cb_.empty()) return;
  timeout->cb_(status);
}

template <class F>
void Timer::At(TimeStamp t, F f) {
  struct CB {
    CB(F f) : fn(std::move(f)) {}
    Optional<Timeout> timeout;
    F fn;
    bool done = false;
    bool initialized = false;
  };
  auto* cb = new CB(std::move(f));
  cb->timeout.Reset(this, t, [cb](const Status& status) {
    if (status.is_ok()) {
      cb->fn();
    }
    cb->done = true;
    if (cb->done && cb->initialized) {
      delete cb;
    }
  });
  cb->initialized = true;
  if (cb->done && cb->initialized) {
    delete cb;
  }
}

}  // namespace overnet
