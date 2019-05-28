// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/testing/test_timer.h"

#include <mutex>

namespace overnet {

static std::mutex mu;

TestTimer::~TestTimer() {
  shutting_down_ = true;
  for (auto tmr : pending_timeouts_) {
    assert(tmr.first > now_);
    FireTimeout(tmr.second, Status::Cancelled());
  }
}

TimeStamp TestTimer::Now() {
  std::lock_guard<std::mutex> lock(mu);
  if (shutting_down_)
    return TimeStamp::AfterEpoch(TimeDelta::PositiveInf());
  return TimeStamp::AfterEpoch(TimeDelta::FromMicroseconds(now_));
}

bool TestTimer::Step(uint64_t microseconds) {
  {
    std::lock_guard<std::mutex> lock(mu);
    if (shutting_down_)
      return false;
    auto new_now = now_ + microseconds;
    if (now_ > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
    if (TimeStamp::AfterEpoch(TimeDelta::FromMicroseconds(new_now)) <
        TimeStamp::AfterEpoch(TimeDelta::FromMicroseconds(now_))) {
      return false;
    }
    now_ = new_now;
  }
  bool ticked = false;
  while (!pending_timeouts_.empty() &&
         pending_timeouts_.begin()->first <=
             (std::lock_guard<std::mutex>(mu), now_)) {
    ticked = true;
    auto it = pending_timeouts_.begin();
    auto* timeout = it->second;
    pending_timeouts_.erase(it);
    FireTimeout(timeout, Status::Ok());
  }
  return ticked;
}

bool TestTimer::StepUntilNextEvent(Optional<TimeDelta> max_step) {
  if (pending_timeouts_.empty())
    return false;
  int64_t step = (std::lock_guard<std::mutex>(mu),
                  pending_timeouts_.begin()->first - now_);
  if (step < 0) {
    step = 0;
  }
  if (max_step.has_value() && step > max_step->as_us()) {
    step = max_step->as_us();
  }
  return Step(step);
}

void TestTimer::InitTimeout(Timeout* timeout, TimeStamp when) {
  if (shutting_down_) {
    FireTimeout(timeout, Status::Cancelled());
    return;
  }

  *TimeoutStorage<uint64_t>(timeout) = when.after_epoch().as_us();
  pending_timeouts_.emplace(when.after_epoch().as_us(), timeout);
}

void TestTimer::CancelTimeout(Timeout* timeout, Status status) {
  assert(!status.is_ok());

  auto rng = pending_timeouts_.equal_range(*TimeoutStorage<uint64_t>(timeout));
  for (auto it = rng.first; it != rng.second; ++it) {
    if (it->second == timeout) {
      pending_timeouts_.erase(it);
      break;
    }
  }

  FireTimeout(timeout, status);
}

}  // namespace overnet
