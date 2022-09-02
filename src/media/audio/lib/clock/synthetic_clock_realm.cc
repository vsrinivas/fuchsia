// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <cmath>
#include <string>

namespace media_audio {

// static
std::shared_ptr<SyntheticClockRealm> SyntheticClockRealm::Create() {
  struct MakePublicCtor : SyntheticClockRealm {
    MakePublicCtor() : SyntheticClockRealm() {}
  };
  return std::make_shared<MakePublicCtor>();
}

std::shared_ptr<SyntheticClock> SyntheticClockRealm::CreateClock(
    std::string_view name, uint32_t domain, bool adjustable,
    media::TimelineFunction to_clock_mono) {
  return SyntheticClock::Create(name, domain, adjustable, shared_from_this(), to_clock_mono);
}

std::shared_ptr<SyntheticTimer> SyntheticClockRealm::CreateTimer() {
  // Serialize with Advance to avoid lost updates.
  std::lock_guard<std::mutex> lock1(advance_mutex_);
  std::lock_guard<std::mutex> lock2(mutex_);
  GarbageCollectTimers();  // prevent unbounded growth

  auto timer = SyntheticTimer::Create(mono_now_);
  timers_.push_back(timer);
  return timer;
}

zx::time SyntheticClockRealm::now() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mono_now_;
}

void SyntheticClockRealm::AdvanceTo(zx::time mono_now) {
  std::lock_guard<std::mutex> lock(advance_mutex_);
  AdvanceToImpl(mono_now);
}

void SyntheticClockRealm::AdvanceBy(zx::duration mono_diff) {
  std::lock_guard<std::mutex> lock(advance_mutex_);
  AdvanceToImpl(now() + mono_diff);
}

void SyntheticClockRealm::AdvanceToImpl(zx::time target_mono_now) {
  auto mono_now = now();
  FX_CHECK(target_mono_now >= mono_now);

  for (;;) {
    zx::time next_deadline = zx::time::infinite();
    bool has_signal = false;

    // Instead of advancing directly to `target_mono_now`, wait until all timers are sleeping, then
    // compute the next deadline and check if any signals are pending.
    for (auto& weak : timers_) {
      if (auto t = weak.lock(); t) {
        t->WaitUntilSleepingOrStopped();
        auto state = t->CurrentState();
        if (state.stopped) {
          continue;
        }
        if (state.deadline) {
          next_deadline = std::min(*state.deadline, next_deadline);
        }
        if (state.shutdown_set || state.event_set) {
          has_signal = true;
        }
      }
    }

    // If there are signals pending, process those before advancing time. Otherwise advance to the
    // next deadline or `target_mono_now`, whichever is earlier. Stop when there are no signals
    // pending, we've advanced to `target_mono_now`, and the next deadline is in the future.
    if (!has_signal) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (mono_now_ == target_mono_now && next_deadline > target_mono_now) {
        return;
      }
      mono_now_ = std::min(target_mono_now, next_deadline);
      mono_now = mono_now_;
    }

    for (auto& weak : timers_) {
      if (auto t = weak.lock(); t) {
        t->AdvanceTo(mono_now);
      }
    }
  }
}

void SyntheticClockRealm::GarbageCollectTimers() {
  for (auto it = timers_.begin(); it != timers_.end();) {
    if (it->expired()) {
      it = timers_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace media_audio
