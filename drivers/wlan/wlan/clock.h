// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <zx/time.h>

#include <cstdint>

namespace wlan {

class Clock {
   public:
    virtual ~Clock() {}

    virtual zx_time_t Now() const = 0;
};

class SystemClock : public Clock {
   public:
    SystemClock(uint32_t clock_id = ZX_CLOCK_MONOTONIC) : clock_id_(clock_id) {}

    zx_time_t Now() const override { return zx::time::get(clock_id_); }

   private:
    uint32_t clock_id_;
};

class TestClock : public Clock {
   public:
    TestClock() {}

    zx_time_t Now() const override { return now_; }
    void Set(zx_time_t time) { now_ = time; }

   private:
    zx_time_t now_ = 0;
};

}  // namespace wlan
