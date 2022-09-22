// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/clock_snapshot.h"

#include <lib/syslog/cpp/macros.h>

#include <cmath>
#include <string>

namespace media_audio {

ClockSnapshot ClockSnapshots::SnapshotFor(zx_koid_t koid) const {
  auto it = snapshots_.find(koid);
  FX_CHECK(it != snapshots_.end()) << "unknown clock";
  FX_CHECK(it->second.last_snapshot) << "clock has not been snapshot yet (forgot to Update?)";
  return *it->second.last_snapshot;
}

ClockSnapshot ClockSnapshots::SnapshotFor(UnreadableClock clock) const {
  return SnapshotFor(clock.koid());
}

void ClockSnapshots::AddClock(std::shared_ptr<const Clock> clock) {
  auto koid = clock->koid();
  FX_CHECK(
      snapshots_.emplace(koid, ClockInfo{.clock = std::move(clock), .last_snapshot = std::nullopt})
          .second)
      << "clock already added";
}

void ClockSnapshots::RemoveClock(std::shared_ptr<const Clock> clock) {
  FX_CHECK(snapshots_.erase(clock->koid()) > 0) << "unknown clock";
}

void ClockSnapshots::Update(zx::time mono_now) {
  for (auto& [koid, info] : snapshots_) {
    info.last_snapshot = ClockSnapshot(info.clock, mono_now);
  }
}

}  // namespace media_audio
