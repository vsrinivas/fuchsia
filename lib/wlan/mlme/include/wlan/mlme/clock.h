// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/time.h>
#include <zircon/types.h>

#include <cstdint>

namespace wlan {

class Clock {
   public:
    virtual ~Clock() {}

    virtual zx::time Now() const = 0;
};

class SystemClock : public Clock {
   public:
    SystemClock() {}

    zx::time Now() const override { return zx::clock::get_monotonic(); }
};

class TestClock : public Clock {
   public:
    TestClock() {}

    zx::time Now() const override { return now_; }
    void Set(zx::time time) { now_ = time; }

   private:
    zx::time now_;
};

}  // namespace wlan
