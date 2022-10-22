// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/mix_job_context.h"

namespace media_audio {

MixJobContext::MixJobContext(const ClockSnapshots& clocks, zx::time mono_start_time,
                             zx::time mono_deadline)
    : clocks_(clocks), mono_start_time_(mono_start_time), mono_deadline_(mono_deadline) {
  FX_CHECK(mono_start_time < mono_deadline);
}

zx::time MixJobContext::start_time(const UnreadableClock& unreadable_clock) const {
  auto clock = clocks_.SnapshotFor(unreadable_clock);
  return zx::time(clock.to_clock_mono().Inverse().Apply(mono_start_time_.get()));
}

zx::time MixJobContext::deadline(const UnreadableClock& unreadable_clock) const {
  auto clock = clocks_.SnapshotFor(unreadable_clock);
  return zx::time(clock.to_clock_mono().Inverse().Apply(mono_deadline_.get()));
}

}  // namespace media_audio
