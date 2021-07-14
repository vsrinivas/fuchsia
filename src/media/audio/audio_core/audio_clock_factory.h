// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_FACTORY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_FACTORY_H_

#include "src/media/audio/audio_core/audio_clock.h"

namespace media::audio {

//
// AudioClockFactory provides a mechanism for relating all clocks created under a single factory
// instance.
//
// In AudioCore, an AudioClockFactory instance is provided per-Context and facilitates creation of
// AudioClocks across AudioCore. Overriding the AudioClockFactory class takes advantage of the
// single point-of-entry for clock creation, enabling sweeping AudioClock modifications and/or
// stubbing for tests.
//

class AudioClockFactory {
 public:
  virtual ~AudioClockFactory() = default;

  virtual std::unique_ptr<AudioClock> CreateClientAdjustable(zx::clock clock) {
    return std::make_unique<AudioClock>(AudioClock::ClientAdjustable(std::move(clock)));
  }

  virtual std::unique_ptr<AudioClock> CreateClientFixed(zx::clock clock) {
    return std::make_unique<AudioClock>(AudioClock::ClientFixed(std::move(clock)));
  }

  virtual std::unique_ptr<AudioClock> CreateDeviceAdjustable(zx::clock clock, uint32_t domain) {
    return std::make_unique<AudioClock>(AudioClock::DeviceAdjustable(std::move(clock), domain));
  }

  virtual std::unique_ptr<AudioClock> CreateDeviceFixed(zx::clock clock, uint32_t domain) {
    return std::make_unique<AudioClock>(AudioClock::DeviceFixed(std::move(clock), domain));
  }

  //
  // The following are intended to be test-only and overridden in FakeClockFactory.
  //

  virtual std::unique_ptr<AudioClock> CreateClientAdjustable(zx::time start_time,
                                                             int32_t rate_adjust_ppm) {
    FX_CHECK(false) << "Custom CreateClientAdjustable not available for real clocks.";
    return nullptr;
  }

  virtual std::unique_ptr<AudioClock> CreateClientFixed(zx::time start_time,
                                                        int32_t rate_adjust_ppm) {
    FX_CHECK(false) << "Custom CreateClientFixed not available for real clocks.";
    return nullptr;
  }

  virtual std::unique_ptr<AudioClock> CreateDeviceAdjustable(zx::time start_time,
                                                             int32_t rate_adjust_ppm,
                                                             uint32_t domain) {
    FX_CHECK(false) << "Custom CreateDeviceAdjustable not available for real clocks.";
    return nullptr;
  }

  virtual std::unique_ptr<AudioClock> CreateDeviceFixed(zx::time start_time,
                                                        int32_t rate_adjust_ppm, uint32_t domain) {
    FX_CHECK(false) << "Custom CreateDeviceFixed not available for real clocks.";
    return nullptr;
  }

  virtual void AdvanceMonoTimeBy(zx::duration duration) {
    FX_CHECK(false) << "AdvanceMonoTimeBy is not available for real clocks.";
  }

  virtual zx::time mono_time() const { return zx::clock::get_monotonic(); }
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_FACTORY_H_
