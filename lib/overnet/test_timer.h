// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include "timer.h"

namespace overnet {

// A timer for testing purposes
class TestTimer final : public Timer {
 public:
  TestTimer(uint64_t start = 0) : now_(start) {}
  ~TestTimer();
  virtual TimeStamp Now() override;
  bool Step(uint64_t microseconds);
  bool StepUntilNextEvent(uint64_t jitter_us = 0);

 private:
  virtual void InitTimeout(Timeout* timeout, TimeStamp when) override;
  virtual void CancelTimeout(Timeout* timeout, Status status) override;

  uint64_t now_;
  std::multimap<uint64_t, Timeout*> pending_timeouts_;
};

}  // namespace overnet
