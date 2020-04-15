// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_driver.h"

#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/testing/fake_audio_device.h"
#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"

namespace media::audio {
namespace {

class AudioDriverTest : public testing::ThreadingModelFixture {
 public:
  void SetUp() override {
    zx::channel c1, c2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));
    remote_driver_ = std::make_unique<testing::FakeAudioDriver>(std::move(c1), dispatcher());

    // Set the fake fifo depth and external delays to something non-zero, just
    // to keep things interesting.
    remote_driver_->set_fifo_depth(kTestFifoDepthFrames * kTestChannels * 2);
    remote_driver_->set_external_delay(kTestExternalDelay);

    ASSERT_EQ(ZX_OK, driver_.Init(std::move(c2)));
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
  AudioDriver driver_{device_.get(), [this](auto delay) { last_late_command_ = delay; }};
  // While |driver_| is the object under test, this object simulates the channel messages that
  // normally come from the actual driver instance.
  std::unique_ptr<testing::FakeAudioDriver> remote_driver_;
  zx::duration last_late_command_ = zx::duration::infinite();

  fzl::VmoMapper mapped_ring_buffer_;
};

TEST_F(AudioDriverTest, GetDriverInfo) {
  remote_driver_->Start();

  driver_.GetDriverInfo();
  RunLoopUntilIdle();
  EXPECT_TRUE(device_->driver_info_fetched());
  EXPECT_EQ(driver_.state(), AudioDriver::State::Unconfigured);
}

TEST_F(AudioDriverTest, GetDriverInfoTimeout) {
  remote_driver_->Stop();

  driver_.GetDriverInfo();

  // DriverInfo still pending.
  RunLoopFor(AudioDriver::kDefaultShortCmdTimeout - zx::nsec(1));
  EXPECT_FALSE(device_->driver_info_fetched());
  EXPECT_EQ(driver_.state(), AudioDriver::State::MissingDriverInfo);

  // Now time out (run 10ms past the deadline).
  RunLoopFor(zx::msec(10) + zx::nsec(1));
  EXPECT_FALSE(device_->driver_info_fetched());
  EXPECT_EQ(driver_.state(), AudioDriver::State::MissingDriverInfo);
  EXPECT_EQ(last_late_command_, zx::duration::infinite());

  // Now run the driver to process the response.
  remote_driver_->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(last_late_command_, zx::msec(10));
  EXPECT_TRUE(device_->driver_info_fetched());
  EXPECT_EQ(driver_.state(), AudioDriver::State::Unconfigured);
}

TEST_F(AudioDriverTest, SanityCheckTimelineMath) {
  // In order to sanity check the timeline math done by the audio driver, we
  // need to march it pretty much all of the way through the configure/startup
  // state machine.  Only after it has been completely configured and started
  // will it have all of the numbers needed to compute the functions to be
  // tested in the first place.
  zx_status_t res;
  remote_driver_->Start();

  // Advance our fake time by some amount, just so we are not doing all of our
  // calculations with a boring start time of 0.
  RunLoopFor(zx::nsec(12'345'967'127));

  // Start by fetching the driver info.  The class will not allow us to
  // configure it unless it has fetched its simulated format list.
  res = driver_.GetDriverInfo();
  ASSERT_EQ(res, ZX_OK);
  RunLoopUntilIdle();
  ASSERT_TRUE(device_->driver_info_fetched());
  ASSERT_EQ(driver_.state(), AudioDriver::State::Unconfigured);

  // Now tell it to configure itself using a format we know will be on its fake
  // format list, and a ring buffer size we know it will be able to give us.
  fuchsia::media::AudioStreamType fidl_format;
  fidl_format.sample_format = kTestSampleFormat;
  fidl_format.channels = kTestChannels;
  fidl_format.frames_per_second = kTestFramesPerSec;

  auto format = Format::Create(fidl_format);
  ASSERT_TRUE(format.is_ok());
  res = driver_.Configure(format.value(), kTestRingBufferMinDuration);
  ASSERT_EQ(res, ZX_OK);

  RunLoopUntilIdle();
  ASSERT_TRUE(device_->driver_config_complete());
  ASSERT_EQ(driver_.state(), AudioDriver::State::Configured);

  // Finally, tell the driver to start.  This will establish the start time and
  // allow the driver to compute the various transformations it will expose to
  // the rest of the system.
  res = driver_.Start();
  ASSERT_EQ(res, ZX_OK);
  RunLoopUntilIdle();
  ASSERT_TRUE(device_->driver_start_complete());
  ASSERT_EQ(driver_.state(), AudioDriver::State::Started);

  const auto& ptscts_ref_clock_to_fractional_frames =
      driver_.ptscts_ref_clock_to_fractional_frames();
  const auto& safe_read_or_write_ref_clock_to_frames =
      driver_.safe_read_or_write_ref_clock_to_frames();

  // Get the driver's external delay and fifo depth expressed in frames.
  uint32_t fifo_depth_frames = driver_.fifo_depth_frames();
  zx::duration external_delay = driver_.external_delay();

  // The fifo depth and external delay had better match what we told the fake
  // driver to report.
  ASSERT_EQ(kTestFifoDepthFrames, fifo_depth_frames);
  ASSERT_EQ(kTestExternalDelay, external_delay);

  // At startup, the tx/rx position should be 0, and the safe read/write position
  // should be fifo_depth_frames ahead of this.
  zx::time now = driver_.start_time();
  EXPECT_EQ(fifo_depth_frames, safe_read_or_write_ref_clock_to_frames.Apply(now.get()));

  // After |external_delay| has passed, we should be at frame zero in the
  // pts/cts timeline.
  now += external_delay;
  EXPECT_EQ(0, ptscts_ref_clock_to_fractional_frames.Apply(now.get()));

  // Advance time by an arbitrary amount and sanity check the results of the
  // various transformations against each other.
  constexpr zx::duration kSomeTime = zx::usec(87654321);
  now += kSomeTime;

  // The safe_read_write_pos should still be fifo_depth_frames ahead of whatever
  // the tx/rx position is, so the tx/rx position should be the safe read/write
  // position minus the fifo depth (in frames).
  //
  // After external_delay has passed, the computed tx/rx position should match
  // the pts/ctx position.  Note, we need convert the fractional frames result
  // of the pts/cts position to integer frames, rounding down in the process, in
  // order to compare the two.
  int64_t safe_read_write_pos = safe_read_or_write_ref_clock_to_frames.Apply(now.get());
  int64_t txrx_pos = safe_read_write_pos - fifo_depth_frames;

  now += external_delay;
  int64_t ptscts_pos_frames = ptscts_ref_clock_to_fractional_frames.Apply(now.get()) /
                              FractionalFrames<uint32_t>(1).raw_value();
  EXPECT_EQ(txrx_pos, ptscts_pos_frames);
}

}  // namespace
}  // namespace media::audio
