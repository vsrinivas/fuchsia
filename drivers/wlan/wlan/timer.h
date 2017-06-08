// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "clock.h"

#include <magenta/types.h>
#include <mx/timer.h>

namespace wlan {

class Timer {
  public:
    explicit Timer(uint64_t id);
    virtual ~Timer();

    virtual mx_time_t Now() const = 0;

    mx_status_t StartTimer(mx_time_t deadline);
    mx_status_t CancelTimer();

    uint64_t id() const { return id_; }
    mx_time_t deadline() const { return deadline_; }

  protected:
    virtual mx_status_t StartTimerImpl(mx_time_t deadline) = 0;
    virtual mx_status_t CancelTimerImpl() = 0;

  private:
    uint64_t id_;
    mx_time_t deadline_ = 0;
};

class SystemTimer final : public Timer {
  public:
    SystemTimer(uint64_t id, mx::timer timer);

    mx_time_t Now() const override { return clock_.Now(); }

  protected:
    mx_status_t StartTimerImpl(mx_time_t deadline) override;
    mx_status_t CancelTimerImpl() override;

  private:
    SystemClock clock_;
    mx::timer timer_;
};

class TestTimer final : public Timer {
  public:
    TestTimer(uint64_t id, TestClock* clock) : Timer(id), clock_(clock) {}

    mx_time_t Now() const override { return clock_->Now(); }

  protected:
    mx_status_t StartTimerImpl(mx_time_t duration) override;
    mx_status_t CancelTimerImpl() override;

  private:
    TestClock* clock_;
};

}  // namespace wlan
