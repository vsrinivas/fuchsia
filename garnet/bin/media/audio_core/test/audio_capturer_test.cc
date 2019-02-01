// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include <lib/gtest/real_loop_fixture.h>

#include "garnet/bin/media/audio_core/test/audio_fidl_tests_shared.h"
#include "lib/component/cpp/environment_services_helper.h"

namespace media::audio::test {

//
// AudioCapturerTest
//
// This set of tests verifies asynchronous usage of AudioCapturer.
class AudioCapturerTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override;
  void TearDown() override;
  void SetNegativeExpectations();
  bool ExpectCallback();
  bool ExpectTimeout();
  bool ExpectDisconnect();

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;
  fuchsia::media::GainControlPtr gain_control_;

  bool error_occurred_ = false;
  bool expect_error_ = false;
  bool expect_capturer_ = true;
  bool received_callback_ = false;
};

//
// AudioCapturerTest implementation
//
void AudioCapturerTest::SetUp() {
  ::gtest::RealLoopFixture::SetUp();

  auto err_handler = [this](zx_status_t error) { error_occurred_ = true; };

  environment_services_ = component::GetEnvironmentServices();
  environment_services_->ConnectToService(audio_.NewRequest());
  audio_.set_error_handler(err_handler);

  audio_->CreateAudioCapturer(audio_capturer_.NewRequest(), false);
  audio_capturer_.set_error_handler(err_handler);
}

void AudioCapturerTest::SetNegativeExpectations() {
  expect_error_ = true;
  expect_capturer_ = false;
}

void AudioCapturerTest::TearDown() {
  ASSERT_TRUE(audio_.is_bound());
  EXPECT_EQ(expect_error_, error_occurred_);
  EXPECT_EQ(expect_capturer_, audio_capturer_.is_bound());

  ::gtest::RealLoopFixture::TearDown();
}

bool AudioCapturerTest::ExpectCallback() {
  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() { return error_occurred_ || received_callback_; },
      kDurationResponseExpected, kDurationGranularity);

  EXPECT_FALSE(error_occurred_);
  EXPECT_TRUE(audio_.is_bound());
  EXPECT_TRUE(audio_capturer_.is_bound());

  EXPECT_FALSE(timed_out);

  EXPECT_TRUE(received_callback_);

  bool return_val = !error_occurred_ && !timed_out;

  received_callback_ = false;
  return return_val;
}

bool AudioCapturerTest::ExpectTimeout() {
  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() { return error_occurred_ || received_callback_; },
      kDurationTimeoutExpected);

  EXPECT_FALSE(error_occurred_);
  EXPECT_TRUE(audio_.is_bound());
  EXPECT_TRUE(audio_capturer_.is_bound());

  EXPECT_TRUE(timed_out);

  EXPECT_FALSE(received_callback_);

  bool return_val = !error_occurred_ && !received_callback_;

  received_callback_ = false;
  return return_val;
}

bool AudioCapturerTest::ExpectDisconnect() {
  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() { return received_callback_ || !audio_capturer_.is_bound(); },
      kDurationResponseExpected, kDurationGranularity);

  EXPECT_TRUE(error_occurred_);
  EXPECT_TRUE(audio_.is_bound());
  EXPECT_FALSE(audio_capturer_.is_bound());

  EXPECT_FALSE(timed_out);

  EXPECT_FALSE(received_callback_);

  bool return_val = !received_callback_ && !timed_out;

  received_callback_ = false;
  return return_val;
}

//
// AudioCapturer implements the base classes StreamBufferSet and StreamSource.

//
// StreamBufferSet validation
//
// TODO(mpuryear): test AddPayloadBuffer(uint32 id, handle<vmo> payload_buffer);
// Also negative testing: bad id, null or bad handle

// TODO(mpuryear): test RemovePayloadBuffer(uint32 id);
// Also negative testing: unknown or already-removed id

// TODO(mpuryear): apply same tests to AudioRenderer and AudioCapturer
// (although their implementations within AudioCore differ somewhat).

//
// StreamSource validation
//

// TODO(mpuryear): test -> OnPacketProduced(StreamPacket packet);
// Always received for every packet - even malformed ones?

// TODO(mpuryear): test -> OnEndOfStream();
// Also proper sequence vis-a-vis other completion and disconnect callbacks

// TODO(mpuryear): test ReleasePacket(StreamPacket packet);
// Also negative testing: malformed or non-submitted packet, before started

// It is an error to call DiscardAllPackets in any of the following conditions:
// 1) when "waiting for VMO" (before AddPayloadBuffer has been called),
// 2) when capturing in Async mode (or during the process of stopping Async),
// 3) while the capture stream is being closed.
// This test case verifies the scenario #1 above.
// TODO(mpuryear): test sequence of pkt return, during Async capture.
TEST_F(AudioCapturerTest, DiscardAllWithNone) {
  SetNegativeExpectations();

  audio_capturer_->DiscardAllPackets([this]() { received_callback_ = true; });

  EXPECT_TRUE(ExpectDisconnect());
}

// TODO(mpuryear): DiscardAllPacketsNoReply() w/no pkt, when started, post-stop
TEST_F(AudioCapturerTest, DiscardAllNoReplyWithNone) {
  SetNegativeExpectations();

  audio_capturer_->DiscardAllPacketsNoReply();

  EXPECT_TRUE(ExpectDisconnect());
}

//
// AudioCapturer validation
//

// TODO(mpuryear): test SetPcmStreamType(AudioStreamType stream_type);
// Also when already set, when packets submitted, when started
// Also negative testing: malformed type

// TODO(mpuryear): test CaptureAt(uint32 id, uint32 offset, uint32 frames)
//                        -> (StreamPacket captured_packet);
// Also when in async capture, before format set, before packets submitted
// Also negative testing: bad id, bad offset, 0/tiny/huge num frames

// TODO(mpuryear): test StartAsyncCapture(uint32 frames_per_packet);
// Also when already started, before format set, before packets submitted
// Also negative testing: 0/tiny/huge num frames (bigger than packet)

TEST_F(AudioCapturerTest, StopWhenStoppedCausesDisconnect) {
  SetNegativeExpectations();

  audio_capturer_->StopAsyncCapture([this]() { received_callback_ = true; });

  EXPECT_TRUE(ExpectDisconnect());
}
// Also test before format set, before packets submitted

TEST_F(AudioCapturerTest, StopNoReplyWhenStoppedCausesDisconnect) {
  SetNegativeExpectations();

  audio_capturer_->StopAsyncCaptureNoReply();

  EXPECT_TRUE(ExpectDisconnect());
}
// Also before format set, before packets submitted

// Null/malformed requests to BindGainControl should have no effect.
TEST_F(AudioCapturerTest, BindGainControlNull) {
  // Passing null request has no effect.
  audio_capturer_->BindGainControl(nullptr);

  // Malformed request should also have no effect.
  auto err_handler = [this](zx_status_t error) { error_occurred_ = true; };
  fuchsia::media::AudioCapturerPtr audio_capturer_2;
  audio_->CreateAudioCapturer(audio_capturer_2.NewRequest(), false);
  audio_capturer_2.set_error_handler(err_handler);

  fidl::InterfaceRequest<fuchsia::media::GainControl> bad_request;
  uint32_t garbage = 0xF0B4783C;
  memmove(&bad_request, &garbage, sizeof(uint32_t));
  audio_capturer_2->BindGainControl(std::move(bad_request));

  // Give time for Disconnect to occur, if it must.
  EXPECT_TRUE(ExpectTimeout());

  EXPECT_TRUE(audio_.is_bound());
  EXPECT_TRUE(audio_capturer_.is_bound());
  EXPECT_TRUE(audio_capturer_2.is_bound());
}

// TODO(mpuryear): test GetStreamType() -> (StreamType stream_type);
// Also negative testing: before format set

}  // namespace media::audio::test
