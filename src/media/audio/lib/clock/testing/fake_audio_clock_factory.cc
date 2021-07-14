// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/testing/fake_audio_clock_factory.h"

#include "src/media/audio/lib/clock/testing/clock_test.h"
#include "src/media/audio/lib/clock/testing/fake_audio_clock.h"

namespace media::audio::testing {

std::unique_ptr<AudioClock> FakeAudioClockFactory::CreateClientAdjustable(zx::clock clock) {
  UpdateRefToMonoTransform(clock);
  return std::make_unique<FakeAudioClock>(
      FakeAudioClock::ClientAdjustable(shared_from_this(), std::move(clock)));
}

std::unique_ptr<AudioClock> FakeAudioClockFactory::CreateClientFixed(zx::clock clock) {
  UpdateRefToMonoTransform(clock);
  return std::make_unique<FakeAudioClock>(
      FakeAudioClock::ClientFixed(shared_from_this(), std::move(clock)));
}

std::unique_ptr<AudioClock> FakeAudioClockFactory::CreateDeviceAdjustable(zx::clock clock,
                                                                          uint32_t domain) {
  UpdateRefToMonoTransform(clock);
  return std::make_unique<FakeAudioClock>(
      FakeAudioClock::DeviceAdjustable(shared_from_this(), std::move(clock), domain));
}

std::unique_ptr<AudioClock> FakeAudioClockFactory::CreateDeviceFixed(zx::clock clock,
                                                                     uint32_t domain) {
  UpdateRefToMonoTransform(clock);
  return std::make_unique<FakeAudioClock>(
      FakeAudioClock::DeviceFixed(shared_from_this(), std::move(clock), domain));
}

std::unique_ptr<AudioClock> FakeAudioClockFactory::CreateClientAdjustable(zx::time start_time,
                                                                          int32_t rate_adjust_ppm) {
  auto clock = clock::testing::CreateCustomClock(
                   {.start_val = start_time, .rate_adjust_ppm = rate_adjust_ppm})
                   .take_value();
  UpdateRefToMonoTransform(audio::clock::GetKoid(clock), start_time, rate_adjust_ppm);
  return std::make_unique<FakeAudioClock>(
      FakeAudioClock::ClientAdjustable(shared_from_this(), std::move(clock)));
}

std::unique_ptr<AudioClock> FakeAudioClockFactory::CreateClientFixed(zx::time start_time,
                                                                     int32_t rate_adjust_ppm) {
  auto clock = clock::testing::CreateCustomClock(
                   {.start_val = start_time, .rate_adjust_ppm = rate_adjust_ppm})
                   .take_value();
  UpdateRefToMonoTransform(audio::clock::GetKoid(clock), start_time, rate_adjust_ppm);
  return std::make_unique<FakeAudioClock>(
      FakeAudioClock::ClientFixed(shared_from_this(), std::move(clock)));
}

std::unique_ptr<AudioClock> FakeAudioClockFactory::CreateDeviceAdjustable(zx::time start_time,
                                                                          int32_t rate_adjust_ppm,
                                                                          uint32_t domain) {
  auto clock = clock::testing::CreateCustomClock(
                   {.start_val = start_time, .rate_adjust_ppm = rate_adjust_ppm})
                   .take_value();
  UpdateRefToMonoTransform(audio::clock::GetKoid(clock), start_time, rate_adjust_ppm);
  return std::make_unique<FakeAudioClock>(
      FakeAudioClock::DeviceAdjustable(shared_from_this(), std::move(clock), domain));
}

std::unique_ptr<AudioClock> FakeAudioClockFactory::CreateDeviceFixed(zx::time start_time,
                                                                     int32_t rate_adjust_ppm,
                                                                     uint32_t domain) {
  auto clock = clock::testing::CreateCustomClock(
                   {.start_val = start_time, .rate_adjust_ppm = rate_adjust_ppm})
                   .take_value();
  UpdateRefToMonoTransform(audio::clock::GetKoid(clock), start_time, rate_adjust_ppm);
  return std::make_unique<FakeAudioClock>(
      FakeAudioClock::DeviceFixed(shared_from_this(), std::move(clock), domain));
}

void FakeAudioClockFactory::UpdateClockRate(zx_koid_t clock_id, int32_t rate_adjust_ppm) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Update ref_to_mono_time_transform with the new |rate_adjust_ppm|, using the current
  // transform to calculate the reference_time parameter.
  ref_time_to_mono_time_transforms_[clock_id] = TimelineFunction(
      mono_time_.get(), ref_time_to_mono_time_transforms_[clock_id].ApplyInverse(mono_time_.get()),
      1'000'000, 1'000'000 + rate_adjust_ppm);
}

void FakeAudioClockFactory::UpdateRefToMonoTransform(const zx::clock& clock) {
  std::lock_guard<std::mutex> lock(mutex_);
  zx_clock_details_v1_t clock_details;
  clock.get_details(&clock_details);

  // Calculate clock offset from kernel monotonic, which is used to create a
  // |ref_time_to_mono_time_transform| based on fake |mono_time_|.
  auto offset = clock_details.mono_to_synthetic.synthetic_offset -
                clock_details.mono_to_synthetic.reference_offset;

  ref_time_to_mono_time_transforms_[audio::clock::GetKoid(clock)] =
      TimelineFunction(mono_time_.get(), mono_time_.get() + offset,
                       clock_details.mono_to_synthetic.rate.reference_ticks,
                       clock_details.mono_to_synthetic.rate.synthetic_ticks);
}

void FakeAudioClockFactory::UpdateRefToMonoTransform(zx_koid_t clock_id, zx::time start_time,
                                                     int32_t rate_adjust_ppm) {
  std::lock_guard<std::mutex> lock(mutex_);

  ref_time_to_mono_time_transforms_[clock_id] =
      TimelineFunction(mono_time_.get(), start_time.get(), 1'000'000, 1'000'000 + rate_adjust_ppm);
}

}  // namespace media::audio::testing
