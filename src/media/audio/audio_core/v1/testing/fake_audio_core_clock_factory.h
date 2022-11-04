// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_FAKE_AUDIO_CLOCK_FACTORY_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_FAKE_AUDIO_CLOCK_FACTORY_H_

#include <lib/zx/clock.h>

#include <mutex>
#include <unordered_map>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/v1/clock.h"

namespace media::audio::testing {

class FakeAudioCoreClockFactory : public AudioCoreClockFactory {
 public:
  // Create a default clock which can by used when any clock is needed.
  static std::shared_ptr<Clock> DefaultClock();

  std::shared_ptr<Clock> CreateClientAdjustable(zx::clock clock) override;
  std::shared_ptr<Clock> CreateClientFixed(zx::clock clock) override;
  std::shared_ptr<Clock> CreateDeviceAdjustable(zx::clock clock, uint32_t domain) override;
  std::shared_ptr<Clock> CreateDeviceFixed(zx::clock clock, uint32_t domain) override;

  // To create FakeClocks based on custom clock specifications.
  std::shared_ptr<Clock> CreateClientAdjustable(zx::time start_time,
                                                int32_t rate_adjust_ppm) override;
  std::shared_ptr<Clock> CreateClientFixed(zx::time start_time, int32_t rate_adjust_ppm) override;
  std::shared_ptr<Clock> CreateDeviceAdjustable(zx::time start_time, int32_t rate_adjust_ppm,
                                                uint32_t domain) override;
  std::shared_ptr<Clock> CreateDeviceFixed(zx::time start_time, int32_t rate_adjust_ppm,
                                           uint32_t domain) override;

  zx::time mono_time() const override { return realm_->now(); }
  void AdvanceMonoTimeBy(zx::duration duration) override { realm_->AdvanceBy(duration); }

  SyntheticClockRealm& synthetic() override { return *realm_; }

 private:
  TimelineFunction RefToMonoTransform(const zx::clock& clock);
  TimelineFunction RefToMonoTransform(zx::time start_time, int32_t rate_adjust_ppm);

  std::shared_ptr<SyntheticClockRealm> realm_ = SyntheticClockRealm::Create();
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_FAKE_AUDIO_CLOCK_FACTORY_H_
