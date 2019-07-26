// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <limits>
#include <ostream>
#include <type_traits>

#include "src/connectivity/overnet/deprecated/lib/vocabulary/callback.h"
#include "src/connectivity/overnet/deprecated/lib/vocabulary/optional.h"
#include "src/connectivity/overnet/deprecated/lib/vocabulary/time.h"

namespace overnet {

class Timeout;

class Timer {
  friend class Timeout;

 public:
  virtual TimeStamp Now() = 0;

  template <class F>
  void At(TimeStamp t, F f);
  void At(TimeStamp t, StatusCallback cb);

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
  Timeout(Timer* timer, TimeStamp when, StatusCallback cb) : timer_(timer), cb_(std::move(cb)) {
    assert(!cb_.empty());
    timer_->InitTimeout(this, when);
  }

  ~Timeout() {
#ifndef NDEBUG
    assert(!destroyed_);
    destroyed_ = true;
#endif
    if (!cb_.empty())
      Cancel();
    assert(cb_.empty());
  }

  void Cancel(Status status = Status::Cancelled()) {
    timer_->CancelTimeout(this, std::move(status));
  }

 private:
#ifndef NDEBUG
  bool destroyed_ = false;
#endif
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
  if (timeout->cb_.empty())
    return;
  auto cb = std::move(timeout->cb_);
  cb(status);
}

template <class F>
void Timer::At(TimeStamp t, F f) {
  At(t, StatusCallback(ALLOCATED_CALLBACK, [f = std::move(f)](const Status& status) mutable {
       if (status.is_ok()) {
         f();
       }
     }));
}

}  // namespace overnet
