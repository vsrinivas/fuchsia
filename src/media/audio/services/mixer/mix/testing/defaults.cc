// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/testing/defaults.h"

#include <memory>

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"

namespace media_audio {

namespace {
struct Defaults {
  std::shared_ptr<SyntheticClockRealm> clock_realm;
  std::shared_ptr<SyntheticClock> clock;
  ClockSnapshots clock_snapshots;
  std::shared_ptr<MixJobContext> mix_job_ctx;

  Defaults() {
    clock_realm = SyntheticClockRealm::Create();
    clock = clock_realm->CreateClock("default_clock_for_tests", Clock::kMonotonicDomain, false);
    clock_snapshots.AddClock(clock);
    clock_snapshots.Update(clock_realm->now());
    mix_job_ctx = std::make_shared<MixJobContext>(clock_snapshots);
  }
};

Defaults global_defaults;
}  // namespace

MixJobContext& DefaultCtx() { return *global_defaults.mix_job_ctx; }
const ClockSnapshots& DefaultClockSnapshots() { return global_defaults.clock_snapshots; }
zx_koid_t DefaultClockKoid() { return global_defaults.clock->koid(); }

TimelineFunction DefaultPresentationTimeToFracFrame(const Format& format) {
  return TimelineFunction(0, 0, format.frac_frames_per_ns());
}

}  // namespace media_audio
