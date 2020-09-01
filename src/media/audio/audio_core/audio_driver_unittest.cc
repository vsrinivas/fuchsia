// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_driver.h"

#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/testing/audio_clock_helper.h"
#include "src/media/audio/audio_core/testing/fake_audio_device.h"
#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"

namespace media::audio {
namespace {

typedef ::testing::Types<testing::FakeAudioDriverV1, testing::FakeAudioDriverV2> DriverTypes;
TYPED_TEST_SUITE(AudioDriverTest, DriverTypes);

template <typename T>
class AudioDriverTest : public testing::ThreadingModelFixture {
 public:
  void SetUp() override {
    driver_ = CreateAudioDriver<T>();
    zx::channel c1, c2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));
    remote_driver_ = std::make_unique<T>(std::move(c1), dispatcher());

    // Set the fake fifo depth and external delays to something non-zero, just
    // to keep things interesting.
    remote_driver_->set_fifo_depth(kTestFifoDepthFrames * kTestChannels * 2);
    remote_driver_->set_external_delay(kTestExternalDelay);

    ASSERT_EQ(ZX_OK, driver_->Init(std::move(c2)));
    mapped_ring_buffer_ =
        remote_driver_->CreateRingBuffer(kTestRingBufferFrames * kTestChannels * 2);
  }

 protected:
  static constexpr auto kTestSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_16;
  static constexpr uint32_t kTestChannels = 2;
  static constexpr uint32_t kTestFramesPerSec = 48000;
  static constexpr uint32_t kTestFifoDepthFrames = 173;
  static constexpr zx::duration kTestExternalDelay = zx::usec(47376);
  static constexpr zx::duration kTestRingBufferMinDuration = zx::msec(200);
  const size_t kTestRingBufferFrames =
      static_cast<size_t>(std::ceil((static_cast<double>(kTestFramesPerSec) * zx::sec(1).get()) /
                                    kTestRingBufferMinDuration.get()));

  std::shared_ptr<testing::FakeAudioOutput> device_{testing::FakeAudioOutput::Create(
      &threading_model(), &context().device_manager(), &context().link_matrix())};
  std::unique_ptr<AudioDriver> driver_;
  // While |driver_| is the object under test, this object simulates the channel messages that
  // normally come from the actual driver instance.
  std::unique_ptr<T> remote_driver_;
  zx::duration last_late_command_ = zx::duration::infinite();

  fzl::VmoMapper mapped_ring_buffer_;

 private:
  template <typename U>
  std::unique_ptr<AudioDriver> CreateAudioDriver() {}

  template <>
  std::unique_ptr<AudioDriver> CreateAudioDriver<testing::FakeAudioDriverV1>() {
    return std::make_unique<AudioDriverV1>(
        device_.get(), [this](zx::duration delay) { last_late_command_ = delay; });
  }

  template <>
  std::unique_ptr<AudioDriver> CreateAudioDriver<testing::FakeAudioDriverV2>() {
    return std::make_unique<AudioDriverV2>(
        device_.get(), [this](zx::duration delay) { last_late_command_ = delay; });
  }
};

TYPED_TEST(AudioDriverTest, GetDriverInfo) {
  this->remote_driver_->Start();

  this->driver_->GetDriverInfo();
  this->RunLoopUntilIdle();
  EXPECT_TRUE(this->device_->driver_info_fetched());
  EXPECT_EQ(this->driver_->state(), AudioDriver::State::Unconfigured);
}

TYPED_TEST(AudioDriverTest, GetDriverInfoTimeout) {
  this->remote_driver_->Stop();

  this->driver_->GetDriverInfo();

  // DriverInfo still pending.
  this->RunLoopFor(AudioDriver::kDefaultShortCmdTimeout - zx::nsec(1));
  EXPECT_FALSE(this->device_->driver_info_fetched());
  EXPECT_EQ(this->driver_->state(), AudioDriver::State::MissingDriverInfo);

  // Now time out (run 10ms past the deadline).
  this->RunLoopFor(zx::msec(10) + zx::nsec(1));
  EXPECT_FALSE(this->device_->driver_info_fetched());
  EXPECT_EQ(this->driver_->state(), AudioDriver::State::MissingDriverInfo);
  EXPECT_EQ(this->last_late_command_, zx::duration::infinite());

  // Now run the driver to process the response.
  this->remote_driver_->Start();
  this->RunLoopUntilIdle();
  EXPECT_EQ(this->last_late_command_, zx::msec(10));
  EXPECT_TRUE(this->device_->driver_info_fetched());
  EXPECT_EQ(this->driver_->state(), AudioDriver::State::Unconfigured);
}

TYPED_TEST(AudioDriverTest, SanityCheckTimelineMath) {
  // In order to sanity check the timeline math done by the audio driver, we
  // need to march it pretty much all of the way through the configure/startup
  // state machine.  Only after it has been completely configured and started
  // will it have all of the numbers needed to compute the functions to be
  // tested in the first place.
  zx_status_t res;
  this->remote_driver_->Start();

  // Advance our fake time by some amount, just so we are not doing all of our
  // calculations with a boring start time of 0.
  this->RunLoopFor(zx::nsec(12'345'967'127));

  // Start by fetching the driver info.  The class will not allow us to
  // configure it unless it has fetched its simulated format list.
  res = this->driver_->GetDriverInfo();
  ASSERT_EQ(res, ZX_OK);
  this->RunLoopUntilIdle();
  ASSERT_TRUE(this->device_->driver_info_fetched());
  ASSERT_EQ(this->driver_->state(), AudioDriver::State::Unconfigured);

  // Now tell it to configure itself using a format we know will be on its fake
  // format list, and a ring buffer size we know it will be able to give us.
  fuchsia::media::AudioStreamType fidl_format;
  fidl_format.sample_format = this->kTestSampleFormat;
  fidl_format.channels = this->kTestChannels;
  fidl_format.frames_per_second = this->kTestFramesPerSec;

  auto format = Format::Create(fidl_format);
  ASSERT_TRUE(format.is_ok());
  res = this->driver_->Configure(format.value(), this->kTestRingBufferMinDuration);
  ASSERT_EQ(res, ZX_OK);

  this->RunLoopUntilIdle();
  ASSERT_TRUE(this->device_->driver_config_complete());
  ASSERT_EQ(this->driver_->state(), AudioDriver::State::Configured);

  // Finally, tell the driver to start.  This will establish the start time and
  // allow the driver to compute the various transformations it will expose to
  // the rest of the system.
  res = this->driver_->Start();
  ASSERT_EQ(res, ZX_OK);
  this->RunLoopUntilIdle();
  ASSERT_TRUE(this->device_->driver_start_complete());
  ASSERT_EQ(this->driver_->state(), AudioDriver::State::Started);

  const auto& ref_time_to_frac_presentation_frame =
      this->driver_->ref_time_to_frac_presentation_frame();
  const auto& ref_time_to_frac_safe_read_or_write_frame =
      this->driver_->ref_time_to_frac_safe_read_or_write_frame();

  // Get the driver's external delay and fifo depth expressed in frames.
  uint32_t fifo_depth_frames = this->driver_->fifo_depth_frames();
  zx::duration external_delay = this->driver_->external_delay();

  // The fifo depth and external delay had better match what we told the fake
  // driver to report.
  ASSERT_EQ(this->kTestFifoDepthFrames, fifo_depth_frames);
  ASSERT_EQ(this->kTestExternalDelay, external_delay);

  // At startup, the tx/rx position should be 0, and the safe read/write position
  // should be fifo_depth_frames ahead of this.
  zx::time ref_now = this->driver_->ref_start_time();
  auto frac_frame = Fixed::FromRaw(ref_time_to_frac_safe_read_or_write_frame.Apply(ref_now.get()));
  EXPECT_EQ(fifo_depth_frames, frac_frame.Floor());

  // After |external_delay| has passed, we should be at frame zero in the
  // pts/cts timeline.
  ref_now += external_delay;
  EXPECT_EQ(0, ref_time_to_frac_presentation_frame.Apply(ref_now.get()));

  // Advance time by an arbitrary amount and sanity check the results of the
  // various transformations against each other.
  constexpr zx::duration kSomeTime = zx::usec(87654321);
  ref_now += kSomeTime;

  // The safe_read_write_pos should still be fifo_depth_frames ahead of whatever
  // the tx/rx position is, so the tx/rx position should be the safe read/write
  // position minus the fifo depth (in frames).
  //
  // After external_delay has passed, the computed tx/rx position should match
  // the pts/ctx position.  Note, we need convert the fractional frames result
  // of the pts/cts position to integer frames, rounding down in the process, in
  // order to compare the two.
  int64_t safe_read_write_pos =
      Fixed::FromRaw(ref_time_to_frac_safe_read_or_write_frame.Apply(ref_now.get())).Floor();
  int64_t txrx_pos = safe_read_write_pos - fifo_depth_frames;

  ref_now += external_delay;
  int64_t ptscts_pos_frames =
      ref_time_to_frac_presentation_frame.Apply(ref_now.get()) / Fixed(1).raw_value();
  EXPECT_EQ(txrx_pos, ptscts_pos_frames);
}

TYPED_TEST(AudioDriverTest, RefClockIsAdvancing) {
  ASSERT_TRUE(this->driver_->reference_clock().is_valid());
  audio_clock_helper::VerifyAdvances(this->driver_->reference_clock());
}

TYPED_TEST(AudioDriverTest, DefaultClockIsClockMono) {
  ASSERT_TRUE(this->driver_->reference_clock().is_valid());
  audio_clock_helper::VerifyIsSystemMonotonic(this->driver_->reference_clock());
}

}  // namespace
}  // namespace media::audio
