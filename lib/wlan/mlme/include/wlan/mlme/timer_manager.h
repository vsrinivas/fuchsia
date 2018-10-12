// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/timer.h>

#include <fbl/unique_ptr.h>

#include <memory>
#include <queue>

namespace wlan {

class TimerManager;

class TimedEvent {
  public:
    static constexpr zx::time kInactive = zx::time();

    TimedEvent(zx::time deadline) : deadline_(deadline) {}
    TimedEvent() : deadline_(kInactive) {}

    zx::time Deadline() const { return deadline_; }
    bool IsActive() const;
    bool Triggered(zx::time now) const;

    void Cancel() { deadline_ = kInactive; }

 private:
    zx::time deadline_;
};

class TimerManager {
  public:
    TimerManager(fbl::unique_ptr<Timer> timer);
    TimerManager(TimerManager&&) = default;
    ~TimerManager();

    // Schedules a new event which expires at the given |deadline|.
    zx_status_t Schedule(zx::time deadline, TimedEvent* event);
    // Needs to be invoked whenever the |timer| met its set deadline. Returns |now|.
    zx::time HandleTimeout();
    // Returns the underlying timer's current time.
    zx::time Now() const { return timer_->Now(); }

    Timer* timer() { return timer_.get(); }

  private:
    void CleanUp(zx::time now);
    std::priority_queue<zx::time, std::vector<zx::time>, std::greater<zx::time>> events_;
    fbl::unique_ptr<Timer> timer_;
};

}  // namespace wlan
