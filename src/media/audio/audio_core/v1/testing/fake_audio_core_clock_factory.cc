// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/testing/fake_audio_core_clock_factory.h"

#include "src/media/audio/lib/clock/testing/clock_test.h"

namespace media::audio::testing {

namespace {
FakeAudioCoreClockFactory kDefaultClockFactory;
}

// static
std::shared_ptr<Clock> FakeAudioCoreClockFactory::DefaultClock() {
  return kDefaultClockFactory.CreateClientFixed(zx::time(0), 0);
}

std::shared_ptr<Clock> FakeAudioCoreClockFactory::CreateClientAdjustable(zx::clock clock) {
  return realm_->CreateClock("synthetic_client_adjustable", Clock::kExternalDomain, true,
                             RefToMonoTransform(clock));
}

std::shared_ptr<Clock> FakeAudioCoreClockFactory::CreateClientFixed(zx::clock clock) {
  return realm_->CreateClock("synthetic_client_fixed", Clock::kExternalDomain, false,
                             RefToMonoTransform(clock));
}

std::shared_ptr<Clock> FakeAudioCoreClockFactory::CreateDeviceAdjustable(zx::clock clock,
                                                                         uint32_t domain) {
  return realm_->CreateClock("synthetic_device_adjustable", domain, true,
                             RefToMonoTransform(clock));
}

std::shared_ptr<Clock> FakeAudioCoreClockFactory::CreateDeviceFixed(zx::clock clock,
                                                                    uint32_t domain) {
  return realm_->CreateClock("synthetic_device_fixed", domain, false, RefToMonoTransform(clock));
}

std::shared_ptr<Clock> FakeAudioCoreClockFactory::CreateClientAdjustable(zx::time start_time,
                                                                         int32_t rate_adjust_ppm) {
  return realm_->CreateClock("synthetic_client_adjustable", Clock::kExternalDomain, true,
                             RefToMonoTransform(start_time, rate_adjust_ppm));
}

std::shared_ptr<Clock> FakeAudioCoreClockFactory::CreateClientFixed(zx::time start_time,
                                                                    int32_t rate_adjust_ppm) {
  return realm_->CreateClock("synthetic_client_fixed", Clock::kExternalDomain, false,
                             RefToMonoTransform(start_time, rate_adjust_ppm));
}

std::shared_ptr<Clock> FakeAudioCoreClockFactory::CreateDeviceAdjustable(zx::time start_time,
                                                                         int32_t rate_adjust_ppm,
                                                                         uint32_t domain) {
  return realm_->CreateClock("synthetic_device_adjustable", domain, true,
                             RefToMonoTransform(start_time, rate_adjust_ppm));
}

std::shared_ptr<Clock> FakeAudioCoreClockFactory::CreateDeviceFixed(zx::time start_time,
                                                                    int32_t rate_adjust_ppm,
                                                                    uint32_t domain) {
  return realm_->CreateClock("synthetic_device_fixed", domain, false,
                             RefToMonoTransform(start_time, rate_adjust_ppm));
}

TimelineFunction FakeAudioCoreClockFactory::RefToMonoTransform(zx::time start_time,
                                                               int32_t rate_adjust_ppm) {
  return TimelineFunction(mono_time().get(), start_time.get(), 1'000'000,
                          1'000'000 + rate_adjust_ppm);
}

TimelineFunction FakeAudioCoreClockFactory::RefToMonoTransform(const zx::clock& clock) {
  zx_clock_details_v1_t clock_details;
  clock.get_details(&clock_details);

  // Calculate clock offset from kernel monotonic, which is used to create a
  // |ref_time_to_mono_time_transform| based on fake |mono_time_|.
  auto offset = clock_details.mono_to_synthetic.synthetic_offset -
                clock_details.mono_to_synthetic.reference_offset;

  auto now = mono_time();
  return TimelineFunction(now.get(), now.get() + offset,
                          clock_details.mono_to_synthetic.rate.reference_ticks,
                          clock_details.mono_to_synthetic.rate.synthetic_ticks);
}

}  // namespace media::audio::testing
