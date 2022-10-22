// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/testing/defaults.h"

#include <zircon/types.h>

#include <memory>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/clock/clock_synchronizer.h"
#include "src/media/audio/lib/clock/synthetic_clock_realm.h"
#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"

namespace media_audio {

namespace {
struct Defaults {
  std::shared_ptr<SyntheticClockRealm> clock_realm;
  std::shared_ptr<Clock> clock;
  ClockSnapshots clock_snapshots;
  std::shared_ptr<MixJobContext> mix_job_ctx;

  Defaults() {
    clock_realm = SyntheticClockRealm::Create();
    clock = clock_realm->CreateClock("default_clock_for_tests", Clock::kMonotonicDomain, false);
    auto now = clock_realm->now();
    clock_snapshots.AddClock(clock);
    clock_snapshots.Update(now);
    mix_job_ctx = std::make_shared<MixJobContext>(clock_snapshots, now, now + zx::msec(10));
  }
};

Defaults global_defaults;
}  // namespace

MixJobContext& DefaultCtx() { return *global_defaults.mix_job_ctx; }

const ClockSnapshots& DefaultClockSnapshots() { return global_defaults.clock_snapshots; }

std::shared_ptr<Clock> DefaultClock() { return global_defaults.clock; }

UnreadableClock DefaultUnreadableClock() { return UnreadableClock(global_defaults.clock); }

std::shared_ptr<ClockSynchronizer> DefaultClockSync() {
  return ClockSynchronizer::Create(global_defaults.clock, global_defaults.clock,
                                   ClockSynchronizer::Mode::WithMicroSRC);
}

TimelineFunction DefaultPresentationTimeToFracFrame(const Format& format) {
  return TimelineFunction(0, 0, format.frac_frames_per_ns());
}

std::shared_ptr<SimplePacketQueueProducerStage> MakeDefaultPacketQueue(const Format& format,
                                                                       std::string_view name) {
  return std::make_shared<SimplePacketQueueProducerStage>(SimplePacketQueueProducerStage::Args{
      .name = name,
      .format = format,
      .reference_clock = DefaultUnreadableClock(),
  });
}

}  // namespace media_audio
