// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_REAL_TIMER_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_REAL_TIMER_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>

#include <atomic>
#include <mutex>
#include <optional>

#include "src/media/audio/lib/clock/timer.h"

namespace media_audio {

// An implementation of Timer that uses a real clock.
//
// This class is thread safe.
class RealTimer : public Timer {
 public:
  struct Config {
    // See: https://fuchsia.dev/fuchsia-src/concepts/kernel/timer_slack
    uint32_t timer_slack_policy = ZX_TIMER_SLACK_LATE;
    zx::duration timer_slack = zx::nsec(0);
  };

  [[nodiscard]] static std::shared_ptr<RealTimer> Create(Config config);

  // Implementation of Timer.
  void SetEventBit() override;
  void SetShutdownBit() override;
  WakeReason SleepUntil(zx::time deadline) override;
  void Stop() override;

 private:
  explicit RealTimer(Config config);

  const zx::duration slack_;
  zx::timer timer_;
  std::atomic<bool> stopped_{false};
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_REAL_TIMER_H_
