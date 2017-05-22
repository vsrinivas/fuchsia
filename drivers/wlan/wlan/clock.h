// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <mx/time.h>

#include <cstdint>

namespace wlan {

class Clock {
  public:
    virtual ~Clock() {}

    virtual mx_time_t Now() const = 0;
};

class SystemClock : public Clock {
  public:
    SystemClock(uint32_t clock_id = MX_CLOCK_MONOTONIC) : clock_id_(clock_id) {}

    mx_time_t Now() const override { return mx::time::get(clock_id_); }

  private:
    uint32_t clock_id_;
};

class TestClock : public Clock {
  public:
    TestClock() {}

    mx_time_t Now() const override { return now_; }
    void Set(mx_time_t time) { now_ = time; }

  private:
    mx_time_t now_ = 0;
};

}  // namespace wlan
