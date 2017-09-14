// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "clock.h"

#include <zircon/types.h>
#include <zx/timer.h>

namespace wlan {

class Timer {
  public:
    explicit Timer(uint64_t id);
    virtual ~Timer();

    virtual zx_time_t Now() const = 0;

    // TODO(tkilbourn): add slack
    zx_status_t SetTimer(zx_time_t deadline);
    zx_status_t CancelTimer();

    uint64_t id() const { return id_; }
    zx_time_t deadline() const { return deadline_; }

  protected:
    virtual zx_status_t SetTimerImpl(zx_time_t deadline) = 0;
    virtual zx_status_t CancelTimerImpl() = 0;

  private:
    uint64_t id_;
    zx_time_t deadline_ = 0;
};

class SystemTimer final : public Timer {
  public:
    SystemTimer(uint64_t id, zx::timer timer);

    zx_time_t Now() const override { return clock_.Now(); }

  protected:
    zx_status_t SetTimerImpl(zx_time_t deadline) override;
    zx_status_t CancelTimerImpl() override;

  private:
    SystemClock clock_;
    zx::timer timer_;
};

class TestTimer final : public Timer {
  public:
    TestTimer(uint64_t id, TestClock* clock) : Timer(id), clock_(clock) {}

    zx_time_t Now() const override { return clock_->Now(); }

  protected:
    zx_status_t SetTimerImpl(zx_time_t duration) override;
    zx_status_t CancelTimerImpl() override;

  private:
    TestClock* clock_;
};

}  // namespace wlan
