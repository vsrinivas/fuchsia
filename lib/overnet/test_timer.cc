// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_timer.h"

namespace overnet {

TestTimer::~TestTimer() {
  for (auto tmr : pending_timeouts_) {
    assert(tmr.first > now_);
    FireTimeout(tmr.second, Status::Cancelled());
  }
}

TimeStamp TestTimer::Now() {
  return TimeStamp::AfterEpoch(TimeDelta::FromMicroseconds(now_));
}

bool TestTimer::Step(uint64_t microseconds) {
  auto new_now = now_ + microseconds;
  if (now_ > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  if (TimeStamp::AfterEpoch(TimeDelta::FromMicroseconds(new_now)) <
      TimeStamp::AfterEpoch(TimeDelta::FromMicroseconds(now_))) {
    return false;
  }
  now_ = new_now;
  while (!pending_timeouts_.empty() &&
         pending_timeouts_.begin()->first <= now_) {
    auto it = pending_timeouts_.begin();
    auto* timeout = it->second;
    pending_timeouts_.erase(it);
    FireTimeout(timeout, Status::Ok());
  }
  return true;
}

bool TestTimer::StepUntilNextEvent(uint64_t jitter_us) {
  if (pending_timeouts_.empty()) return false;
  Step(pending_timeouts_.begin()->first - now_ +
       (jitter_us ? rand() % jitter_us : 0));
  return true;
}

void TestTimer::InitTimeout(Timeout* timeout, TimeStamp when) {
  if (when.after_epoch().as_us() <= static_cast<int64_t>(now_)) {
    FireTimeout(timeout, Status::Ok());
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