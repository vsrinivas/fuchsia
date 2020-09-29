// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_STOPWATCH_H_
#define SRC_UI_LIB_ESCHER_UTIL_STOPWATCH_H_

#include <chrono>

#include "src/lib/fxl/macros.h"

namespace escher {

class Stopwatch {
  typedef std::chrono::high_resolution_clock ClockT;
  typedef std::chrono::microseconds UnitT;

 public:
  explicit Stopwatch(bool start_immediately = true);
  void Start();
  void Stop();
  void Toggle();
  void Reset();
  uint64_t GetElapsedMicroseconds() const;
  double GetElapsedSeconds() const {
    return 0.000001 * static_cast<double>(GetElapsedMicroseconds());
  }

 private:
  bool is_started_ = false;
  uint64_t elapsed_microseconds_ = 0;
  std::chrono::time_point<ClockT> start_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Stopwatch);
};

// Inline function definitions.

inline Stopwatch::Stopwatch(bool start_immediately) {
  if (start_immediately)
    Start();
}

inline void Stopwatch::Start() {
  if (!is_started_) {
    is_started_ = true;
    start_ = ClockT::now();
    return;
  }
}

inline void Stopwatch::Stop() {
  if (is_started_) {
    is_started_ = false;
    auto stop = ClockT::now();
    auto duration = std::chrono::duration_cast<UnitT>(stop - start_);
    elapsed_microseconds_ += duration.count();
  }
}

inline void Stopwatch::Toggle() {
  if (is_started_)
    Stop();
  else
    Start();
}

inline void Stopwatch::Reset() {
  bool was_started_ = is_started_;
  Stop();
  elapsed_microseconds_ = 0;
  if (was_started_)
    Start();
}

inline uint64_t Stopwatch::GetElapsedMicroseconds() const {
  if (is_started_) {
    auto stop = ClockT::now();
    auto duration = std::chrono::duration_cast<UnitT>(stop - start_);
    return elapsed_microseconds_ + duration.count();
  } else {
    return elapsed_microseconds_;
  }
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_STOPWATCH_H_
