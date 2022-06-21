// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_TIMER_WITH_REAL_CLOCK_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_TIMER_WITH_REAL_CLOCK_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>

#include <mutex>
#include <optional>

#include "src/media/audio/services/mixer/common/timer.h"

namespace media_audio {

// An implementation of Timer that uses a real clock.
// This class is thread safe.
class TimerWithRealClock : public Timer {
 public:
  struct Config {
    // See: https://fuchsia.dev/fuchsia-src/concepts/kernel/timer_slack
    uint32_t timer_slack_policy = ZX_TIMER_SLACK_LATE;
    zx::duration timer_slack = zx::nsec(0);
  };
  explicit TimerWithRealClock(Config config);

  // Implementation of Timer.
  void SetEventBit() override;
  void SetShutdownBit() override;
  WakeReason SleepUntil(zx::time deadline) override;

 private:
  const zx::duration slack_;
  zx::timer timer_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_TIMER_WITH_REAL_CLOCK_H_
