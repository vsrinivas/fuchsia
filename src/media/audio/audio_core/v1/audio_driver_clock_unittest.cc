// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>

#include "src/media/audio/audio_core/v1/audio_device_manager.h"
#include "src/media/audio/audio_core/v1/audio_driver.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_device.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"

namespace media::audio {
namespace {

// Configuration consts for these tests
const Format kFormat =
    Format::Create<fuchsia::media::AudioSampleFormat::SIGNED_16>(2, 48000).value();
const auto kBytesPerFrame = kFormat.bytes_per_frame();

constexpr auto kRingBufferDuration = zx::msec(200);
constexpr size_t kRingBufferFrames = 9600;  // 200 msec at 48k
const size_t kHalfRingBufferBytes = kRingBufferFrames * kBytesPerFrame / 2;

constexpr auto kStartTime = zx::time(0) + zx::msec(500);
constexpr auto kNotificationDuration = zx::msec(100);
constexpr uint32_t kNonMonotonicDomain = 42;

// Test class to verify AudioDriver's clock-related aspects (domain, notifications, clock recovery).
class AudioDriverClockTest : public testing::ThreadingModelFixture {
 protected:
  // Initialize our remote driver after configuring its clock domain; retrieve basic driver info
  void CreateDrivers(uint32_t clock_domain) {
    driver_ = CreateAudioDriver();
    zx::channel for_remote, for_local;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &for_remote, &for_local));
    remote_driver_ =
        std::make_unique<testing::FakeAudioDriver>(std::move(for_remote), dispatcher());

    remote_driver_->set_clock_domain(clock_domain);

    ASSERT_EQ(ZX_OK, driver_->Init(std::move(for_local)));
    mapped_ring_buffer_ = remote_driver_->CreateRingBuffer(kRingBufferFrames * kBytesPerFrame);

    remote_driver_->Start();
    RunLoopUntilIdle();

    ASSERT_EQ(driver_->GetDriverInfo(), ZX_OK);
    RunLoopUntilIdle();
    ASSERT_TRUE(device_->driver_info_fetched());
  }

  // Configure the driver, including establishment of format and ring buffer. Then start the ring
  // buffer after advancing to the given time (so that ref_start_time is correct)
  void ConfigureAndStartDriver() {
    ASSERT_EQ(driver_->Configure(kFormat, kRingBufferDuration), ZX_OK);
    RunLoopUntilIdle();
    ASSERT_TRUE(device_->driver_config_complete());

    RunLoopUntil(kStartTime);
    ASSERT_EQ(driver_->Start(), ZX_OK);

    RunLoopUntilIdle();
    ASSERT_TRUE(device_->driver_start_complete());
    EXPECT_TRUE(remote_driver_->is_running());
    EXPECT_GT(remote_driver_->mono_start_time(), zx::time(0));
  }

  // Validation functions reused for parameterized testing
  void ValidateClockDomainSet(uint32_t clock_domain) {
    CreateDrivers(clock_domain);

    EXPECT_EQ(driver_->clock_domain(), clock_domain);
  }

  // After GetDriverInfo, the clock should already be available and advancing at monotonic rate,
  // regardless of its clock domain (thus regardless of whether it might subsequently diverge).
  // Rate-adjustments occur based on position notifications, emitted after ring buffer is started.
  void ValidateClockAdvancesAtClockMonotonicRate(uint32_t clock_domain) {
    CreateDrivers(clock_domain);

    clock::testing::VerifyAdvances(*driver_->reference_clock(),
                                   context().clock_factory()->synthetic());
    clock::testing::VerifyIsSystemMonotonic(*driver_->reference_clock());
  }

  // Verify that AudioDriver correctly uses driver position notifications to rate-adjust
  // its AudioClock -- but only if the device is in a non-MONOTONIC domain.
  void ValidateNotificationsTuneDriverClock(uint32_t clock_domain, uint32_t notification_position) {
    CreateDrivers(clock_domain);
    ConfigureAndStartDriver();

    // Trigger the remote driver to emit an initial position notification.
    remote_driver_->SendPositionNotification(remote_driver_->mono_start_time(), 0);

    // If MONOTONIC, no position notifications should be delivered.
    auto mono_to_ref = driver_->reference_clock()->to_clock_mono().Inverse();

    // First notification won't lead to adustment: clock still tracks MONOTONIC (numer == denom).
    EXPECT_EQ(mono_to_ref.subject_delta(), mono_to_ref.reference_delta());

    // Trigger a position notification. These values may indicate a rate divergence from MONOTONIC.
    remote_driver_->SendPositionNotification(
        remote_driver_->mono_start_time() + kNotificationDuration, notification_position);
    RunLoopUntilIdle();

    // When we return, if the driver zx::clock has been tuned, then the clock rate numer != denom.
  }

  // the actual object under test
  std::unique_ptr<AudioDriver> driver_;
  // simulates channel messages from the actual driver instance
  std::unique_ptr<testing::FakeAudioDriver> remote_driver_;

  std::shared_ptr<testing::FakeAudioOutput> device_{testing::FakeAudioOutput::Create(
      context().process_config().device_config(), &threading_model(), &context().device_manager(),
      &context().link_matrix(), context().clock_factory())};

  fzl::VmoMapper mapped_ring_buffer_;

 private:
  std::unique_ptr<AudioDriver> CreateAudioDriver() {
    return std::make_unique<AudioDriver>(device_.get(), [](zx::duration) {});
  }
};

// AudioDriver correctly retrieves and caches the clock domain provided by the driver
TEST_F(AudioDriverClockTest, MonotonicClockDomain) {
  this->ValidateClockDomainSet(Clock::kMonotonicDomain);
}

// AudioDriver correctly retrieves and caches the clock domain provided by the driver
TEST_F(AudioDriverClockTest, NonMonotonicClockDomain) {
  this->ValidateClockDomainSet(kNonMonotonicDomain);
}

// For devices in the MONOTONIC domain, the clock is available after GetDriverInfo.
TEST_F(AudioDriverClockTest, DefaultRefClockAdvancesAtMonoRate) {
  this->ValidateClockAdvancesAtClockMonotonicRate(Clock::kMonotonicDomain);
}

// We model clocks in a non-MONOTONIC domain as running at the MONOTONIC rate, until we start
// receiving the position notifications from which we recover its actual rate.
TEST_F(AudioDriverClockTest, NonMonoClockAdvancesAtMonoRate) {
  this->ValidateClockAdvancesAtClockMonotonicRate(kNonMonotonicDomain);
}

// Given notifications suggesting a device runs at monotonic rate, its clock should not be adjusted
TEST_F(AudioDriverClockTest, NotificationsAdjustClockRateSame) {
  this->ValidateNotificationsTuneDriverClock(kNonMonotonicDomain, kHalfRingBufferBytes);

  auto mono_to_ref = this->driver_->reference_clock()->to_clock_mono().Inverse();
  EXPECT_EQ(mono_to_ref.subject_delta(), mono_to_ref.reference_delta());
}

// Given notifications suggesting a device runs slow, its clock should be rate-adjusted downward
TEST_F(AudioDriverClockTest, NotificationsAdjustClockRateDown) {
  // We should be at the ring buffer's halfway point, but we are not quite there yet
  this->ValidateNotificationsTuneDriverClock(kNonMonotonicDomain, kHalfRingBufferBytes * 0.95);

  auto mono_to_ref = this->driver_->reference_clock()->to_clock_mono().Inverse();
  EXPECT_LT(mono_to_ref.subject_delta(), mono_to_ref.reference_delta());
}

// Given notifications suggesting a device runs fast, its clock should be rate-adjusted upward
TEST_F(AudioDriverClockTest, NotificationsAdjustClockRateUp) {
  // We should be at the ring buffer's halfway point, but we are a little farther along
  this->ValidateNotificationsTuneDriverClock(kNonMonotonicDomain, kHalfRingBufferBytes * 1.05);

  auto mono_to_ref = this->driver_->reference_clock()->to_clock_mono().Inverse();
  EXPECT_GT(mono_to_ref.subject_delta(), mono_to_ref.reference_delta());
}

// In the MONOTONIC domain, no position notifications are sent; thus no rate-adjustment can occur.
// These values suggest the device runs fast, so if a notification is sent, the rate will change.
TEST_F(AudioDriverClockTest, NotificationsDontAdjustMonotonicDomain) {
  this->ValidateNotificationsTuneDriverClock(Clock::kMonotonicDomain, kHalfRingBufferBytes * 1.05);

  auto mono_to_ref = this->driver_->reference_clock()->to_clock_mono().Inverse();
  EXPECT_EQ(mono_to_ref.subject_delta(), mono_to_ref.reference_delta());
}

}  // namespace
}  // namespace media::audio
