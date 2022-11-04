// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_CLOCK_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_CLOCK_H_

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/clock/real_clock.h"
#include "src/media/audio/lib/clock/recovered_clock.h"
#include "src/media/audio/lib/clock/synthetic_clock_realm.h"

namespace media::audio {

using Clock = ::media_audio::Clock;
using RealClock = ::media_audio::RealClock;
using SyntheticClock = ::media_audio::SyntheticClock;
using SyntheticClockRealm = ::media_audio::SyntheticClockRealm;
using RecoveredClock = ::media_audio::RecoveredClock;

class AudioCoreClockFactory {
 public:
  virtual ~AudioCoreClockFactory() = default;

  virtual std::shared_ptr<Clock> CreateClientAdjustable(zx::clock clock) {
    return RealClock::Create("client_adjustable", std::move(clock), Clock::kExternalDomain, true);
  }

  virtual std::shared_ptr<Clock> CreateClientFixed(zx::clock clock) {
    return RealClock::Create("client_fixed", std::move(clock), Clock::kExternalDomain, false);
  }

  virtual std::shared_ptr<Clock> CreateDeviceAdjustable(zx::clock clock, uint32_t domain) {
    return RealClock::Create("device_adjustable", std::move(clock), domain, true);
  }

  virtual std::shared_ptr<Clock> CreateDeviceFixed(zx::clock clock, uint32_t domain) {
    return RealClock::Create("device_fixed", std::move(clock), domain, false);
  }

  //
  // The following are intended to be test-only and overridden in FakeClockFactory.
  //

  virtual std::shared_ptr<Clock> CreateClientAdjustable(zx::time start_time,
                                                        int32_t rate_adjust_ppm) {
    FX_CHECK(false) << "Custom CreateClientAdjustable not available for real clocks.";
    return nullptr;
  }

  virtual std::shared_ptr<Clock> CreateClientFixed(zx::time start_time, int32_t rate_adjust_ppm) {
    FX_CHECK(false) << "Custom CreateClientFixed not available for real clocks.";
    return nullptr;
  }

  virtual std::shared_ptr<Clock> CreateDeviceAdjustable(zx::time start_time,
                                                        int32_t rate_adjust_ppm, uint32_t domain) {
    FX_CHECK(false) << "Custom CreateDeviceAdjustable not available for real clocks.";
    return nullptr;
  }

  virtual std::shared_ptr<Clock> CreateDeviceFixed(zx::time start_time, int32_t rate_adjust_ppm,
                                                   uint32_t domain) {
    FX_CHECK(false) << "Custom CreateDeviceFixed not available for real clocks.";
    return nullptr;
  }

  virtual void AdvanceMonoTimeBy(zx::duration duration) {
    FX_CHECK(false) << "AdvanceMonoTimeBy is not available for real clocks.";
  }

  virtual zx::time mono_time() const { return zx::clock::get_monotonic(); }

  virtual SyntheticClockRealm& synthetic() {
    FX_CHECK(false) << "SyntheticClockRealm is not available for real clocks.";
    __builtin_unreachable();
  }
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_CLOCK_H_
