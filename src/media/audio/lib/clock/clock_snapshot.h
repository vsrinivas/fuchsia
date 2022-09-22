// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_CLOCK_SNAPSHOT_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_CLOCK_SNAPSHOT_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/clock/unreadable_clock.h"

namespace media_audio {

// A snapshot of a clock at a single moment in time. The API is similar to that of a `const Clock`.
// This is a value type that supports copy and assignment.
class ClockSnapshot {
 public:
  // Create a snapshot of the given clock at the given monotonic time.
  ClockSnapshot(const std::shared_ptr<const Clock>& clock, zx::time mono_time)
      : backing_clock_(clock),
        to_clock_mono_snapshot_(clock->to_clock_mono_snapshot()),
        mono_now_(mono_time),
        // Use `to_clock_mono_snapshot_` to ensure a consistent snapshot.
        ref_now_(zx::time(to_clock_mono_snapshot_.to_clock_mono.ApplyInverse(mono_time.get()))) {}

  std::string_view name() const { return backing_clock_->name(); }
  zx_koid_t koid() const { return backing_clock_->koid(); }
  uint32_t domain() const { return backing_clock_->domain(); }

  // Returns when the snapshot was taken according to the snapshotted clock.
  zx::time now() const { return ref_now_; }

  // Returns when the snapshot was taken according to the system monotonic clock.
  zx::time mono_now() const { return mono_now_; }

  // Returns the TimelineFunction for the current snapshot.
  Clock::ToClockMonoSnapshot to_clock_mono_snapshot() const { return to_clock_mono_snapshot_; }
  media::TimelineFunction to_clock_mono() const { return to_clock_mono_snapshot().to_clock_mono; }

  // Returns the reference time equivalent to the given system monotonic time.
  zx::time ReferenceTimeFromMonotonicTime(zx::time mono_time) const {
    return zx::time(to_clock_mono().ApplyInverse(mono_time.get()));
  }

  // Returns the system monotonic time equivalent to the given reference time.
  zx::time MonotonicTimeFromReferenceTime(zx::time ref_time) const {
    return zx::time(to_clock_mono().Apply(ref_time.get()));
  }

 private:
  // Hold a shared_ptr to the clock, rather than copying state, to avoid copying the name.
  std::shared_ptr<const Clock> backing_clock_;
  Clock::ToClockMonoSnapshot to_clock_mono_snapshot_;
  zx::time mono_now_;
  zx::time ref_now_;
};

// This class provides a way to snapshot multiple clocks at once.
// Not safe for concurrent use.
class ClockSnapshots {
 public:
  // Returns the most recent snapshot for the clock with the given koid.
  // Update must have been called since this clock was added.
  ClockSnapshot SnapshotFor(zx_koid_t clock_koid) const;
  ClockSnapshot SnapshotFor(UnreadableClock clock) const;

  // Adds a clock to snapshot in future calls to Update.
  //
  // REQUIRES: A clock with the same koid has not already been added.
  void AddClock(std::shared_ptr<const Clock> clock);

  // Removes a clock from this container.
  //
  // REQUIRES: A clock with the same koid has been added.
  void RemoveClock(std::shared_ptr<const Clock> clock);

  // Update the snapshot of every clock.
  void Update(zx::time mono_now);

 private:
  struct ClockInfo {
    std::shared_ptr<const Clock> clock;
    std::optional<ClockSnapshot> last_snapshot;
  };

  std::unordered_map<zx_koid_t, ClockInfo> snapshots_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_CLOCK_SNAPSHOT_H_
