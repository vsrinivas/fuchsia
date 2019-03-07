// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <time.h>
#include <map>
#include <mutex>
#include <unordered_map>
#include "garnet/lib/overnet/environment/timer.h"

namespace overnet {

inline timespec operator-(timespec a, timespec b) {
  timespec out;
  out.tv_sec = a.tv_sec - b.tv_sec;
  out.tv_nsec = a.tv_nsec - b.tv_nsec;
  if (out.tv_nsec < 0) {
    out.tv_nsec += 1000000000;
    out.tv_sec--;
  }
  return out;
}

class MonotonicTimer {
 public:
  MonotonicTimer() { clock_gettime(CLOCK_MONOTONIC, &start_); }

  TimeStamp Now() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    auto dt = now - start_;
    return TimeStamp::AfterEpoch(
        TimeDelta::FromMicroseconds(dt.tv_sec * 1000000 + dt.tv_nsec / 1000));
  }

 private:
  timespec start_;
};

class HostReactor final : public Timer {
 public:
  ~HostReactor();
  virtual TimeStamp Now() override { return source_.Now(); }
  Status Run();

  void OnRead(int fd, StatusCallback cb) { fds_[fd].on_read = std::move(cb); }
  void OnWrite(int fd, StatusCallback cb) { fds_[fd].on_read = std::move(cb); }

  void OnWrite(int fd, StatusCallback cb) { fds_[fd].on_read = std::move(cb); }

 private:
  void InitTimeout(Timeout* timeout, TimeStamp when) override;
  void CancelTimeout(Timeout* timeout, Status status) override;

  MonotonicTimer source_;
  bool shutting_down_ = false;
  std::multimap<TimeStamp, Timeout*> pending_timeouts_;
  struct FDState {
    StatusCallback on_read;
    StatusCallback on_write;
  };
  std::unordered_map<int, FDState> fds_;
};

}  // namespace overnet
