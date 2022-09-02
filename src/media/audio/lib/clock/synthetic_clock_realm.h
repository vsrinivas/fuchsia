// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_SYNTHETIC_CLOCK_REALM_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_SYNTHETIC_CLOCK_REALM_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/clock.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "src/media/audio/lib/clock/synthetic_clock.h"
#include "src/media/audio/lib/clock/synthetic_timer.h"

namespace media_audio {

// Creates and controls a collection of synthetic clocks and timers. Each realm has its own,
// isolated, synthetic monotonic clock, which advances on demand (see `AdvanceTo` and `AdvanceBy`).
// Within a realm, all clocks and timers advance atomically relative to the realm's synthetic
// montonic clock.
//
// All methods are safe to call from any thread.
class SyntheticClockRealm : public std::enable_shared_from_this<SyntheticClockRealm> {
 public:
  // Create a new realm with `now() == zx::time(0)`.
  [[nodiscard]] static std::shared_ptr<SyntheticClockRealm> Create();

  // Creates a new clock. The clock starts starts with the given `to_clock_mono` transformation (by
  // default, the identity transform).
  [[nodiscard]] std::shared_ptr<SyntheticClock> CreateClock(
      std::string_view name, uint32_t domain, bool adjustable,
      media::TimelineFunction to_clock_mono = media::TimelineFunction(0, 0, 1, 1));

  // Creates a new timer.
  [[nodiscard]] std::shared_ptr<SyntheticTimer> CreateTimer();

  // The current synthetic monotonic time.
  [[nodiscard]] zx::time now() const;

  // Advances `now` to the given monotonic time. Time advances in increments, using the following
  // procedure:
  //
  // 1. Wait until every non-stopped timer `i` is blocked in `SleepUntil(t_i)`.
  // 2. If any timer has a shutdown or event bit set, wake those timers and goto 1. Else goto 3.
  // 3. Set `now` to the minimum of all `t_i` and `mono_now`.
  // 4. If any timer has `t_i == now`, wake those timers and goto 1. Else stop.
  //
  // This procedure ensures that time advances deterministically. Timers must eventually block in
  // `SleepUntil` or be `Stop`ed, otherwise AdvanceTo will deadlock. It is legal to call
  // `AdvanceTo(now())`. This runs all pending events without advancing time.
  //
  // Requires: `mono_now >= now()`
  void AdvanceTo(zx::time mono_now);

  // Advances `now` by the given duration. This is equivalent to `AdvanceTo(now() + mono_diff)` but
  // executed atomically.
  //
  // Requires: `mono_diff > 0`
  void AdvanceBy(zx::duration mono_diff);

 private:
  void AdvanceToImpl(zx::time mono_now) TA_REQ(advance_mutex_);
  void GarbageCollectTimers() TA_REQ(advance_mutex_);

  SyntheticClockRealm() = default;

  // The current time is guarded by `mutex_`. Calls to `Advance{To,By}` may block waiting for other
  // threads, so to avoid blocking while holding `mutex_`, we use a different mutex
  // (`advance_mutex_`) to serialize calls to `Advance{To,By}`.
  mutable std::mutex advance_mutex_ TA_ACQ_BEFORE(mutex_);
  mutable std::mutex mutex_;

  // The current time.
  zx::time mono_now_ TA_GUARDED(mutex_);

  // SyntheticClocks are not notified when time advances. Each call to `SyntheticClock::now()` asks
  // the parent realm for the current monotonic time. In contrast, SyntheticTimers are notified by a
  // call to `SyntheticTimer::AdvanceTo`.
  //
  // Hence, we maintain reference counted pointers in these directions:
  //
  // ```
  // SyntheticClock -> SyntheticClockRealm
  // SyntheticClockRealm -> SyntheticTimer(s)
  // ```
  //
  // The pointers from clock to realm are strong and created with `shared_from_this`. The pointers
  // from realm to timer are weak since a timer does not need to be updated when there are no other
  // references.
  //
  // This is guarded by `advance_mutex_` so that CreateTimer calls are serialized with calls to
  // Advance. If we allow CreateTimer and Advance to happen concurrently, then the CreateTimer call
  // may miss an update from the concurrent Advance.
  std::vector<std::weak_ptr<SyntheticTimer>> timers_ TA_GUARDED(advance_mutex_);
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_SYNTHETIC_CLOCK_REALM_H_
