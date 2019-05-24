// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/gtest/real_loop_fixture.h>

#include "lib/component/cpp/environment_services_helper.h"
#include "src/media/audio/audio_core/test/audio_tests_shared.h"

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
  fuchsia::media::audio::GainControlPtr gain_control_;

  bool error_occurred_ = false;
  bool expect_error_ = false;
  bool expect_capturer_ = true;
  bool received_callback_ = false;
};

//
// AudioCapturerTest implementation
//
void AudioCapturerTest::SetUp() {
  gtest::RealLoopFixture::SetUp();

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

  gtest::RealLoopFixture::TearDown();
}

bool AudioCapturerTest::ExpectCallback() {
  received_callback_ = false;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() { return error_occurred_ || received_callback_; },
      kDurationResponseExpected, kDurationGranularity);

  EXPECT_FALSE(error_occurred_);
  EXPECT_TRUE(audio_.is_bound());
  EXPECT_TRUE(audio_capturer_.is_bound());

  EXPECT_FALSE(timed_out);

  EXPECT_TRUE(received_callback_);

  bool return_val = !error_occurred_ && !timed_out;

  return return_val;
}

// TODO(mpuryear): Refactor tests to eliminate "wait for nothing bad to happen".
bool AudioCapturerTest::ExpectTimeout() {
  received_callback_ = false;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() { return error_occurred_ || received_callback_; },
      kDurationTimeoutExpected);

  EXPECT_FALSE(error_occurred_);
  EXPECT_TRUE(audio_.is_bound());
  EXPECT_TRUE(audio_capturer_.is_bound());

  EXPECT_TRUE(timed_out);

  EXPECT_FALSE(received_callback_);

  bool return_val = !error_occurred_ && !received_callback_;

  return return_val;
}

bool AudioCapturerTest::ExpectDisconnect() {
  received_callback_ = false;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() { return received_callback_ || !audio_capturer_.is_bound(); },
      kDurationResponseExpected, kDurationGranularity);

  EXPECT_TRUE(error_occurred_);
  EXPECT_TRUE(audio_.is_bound());
  EXPECT_FALSE(audio_capturer_.is_bound());

  EXPECT_FALSE(timed_out);

  EXPECT_FALSE(received_callback_);

  bool return_val = !received_callback_ && !timed_out;

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
//
TEST_F(AudioCapturerTest, DiscardAllWithNone) {
  SetNegativeExpectations();

  audio_capturer_->DiscardAllPackets([this]() { received_callback_ = true; });

  EXPECT_TRUE(ExpectDisconnect());
}

// TODO(mpuryear): DiscardAllPacketsNoReply() when started, post-stop
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

// Test creation and interface independence of GainControl.
// In a number of tests below, we run the message loop to give the AudioCapturer
// or GainControl binding a chance to disconnect, if an error occurred.
//
// TODO(mpuryear): Refactor tests to eliminate "wait for nothing bad to happen".
TEST_F(AudioCapturerTest, BindGainControl) {
  // Validate AudioCapturers can create GainControl interfaces.
  audio_capturer_->BindGainControl(gain_control_.NewRequest());
  bool gc_error_occurred = false;
  auto gc_err_handler = [&gc_error_occurred](zx_status_t error) {
    gc_error_occurred = true;
  };
  gain_control_.set_error_handler(gc_err_handler);

  fuchsia::media::AudioCapturerPtr audio_capturer_2;
  audio_->CreateAudioCapturer(audio_capturer_2.NewRequest(), true);
  bool ac2_error_occurred = false;
  auto ac2_err_handler = [&ac2_error_occurred](zx_status_t error) {
    ac2_error_occurred = true;
  };
  audio_capturer_2.set_error_handler(ac2_err_handler);

  fuchsia::media::audio::GainControlPtr gain_control_2;
  audio_capturer_2->BindGainControl(gain_control_2.NewRequest());
  bool gc2_error_occurred = false;
  auto gc2_err_handler = [&gc2_error_occurred](zx_status_t error) {
    gc2_error_occurred = true;
  };
  gain_control_2.set_error_handler(gc2_err_handler);

  // Validate GainControl does NOT persist after AudioCapturer is unbound.
  expect_capturer_ = false;
  audio_capturer_.Unbind();

  // Validate that AudioCapturer2 persists without GainControl2.
  gain_control_2.Unbind();

  // ...give the two interfaces a chance to completely unbind...
  EXPECT_FALSE(RunLoopWithTimeoutOrUntil(
      [this, &ac2_error_occurred, &gc2_error_occurred]() {
        return (error_occurred_ || ac2_error_occurred || gc2_error_occurred);
      },
      kDurationTimeoutExpected * 2));

  // Explicitly unbinding audio_capturer_ should not trigger its disconnect
  // (error_occurred_), but should trigger gain_control_'s disconnect.
  EXPECT_TRUE(gc_error_occurred);
  EXPECT_FALSE(gain_control_.is_bound());

  // Explicitly unbinding gain_control_2 should not trigger its disconnect, nor
  // its parent audio_capturer_2's.
  EXPECT_FALSE(ac2_error_occurred);
  EXPECT_FALSE(gc2_error_occurred);
  EXPECT_TRUE(audio_capturer_2.is_bound());
}

// Null/malformed requests to BindGainControl should have no effect.
TEST_F(AudioCapturerTest, BindGainControlNull) {
  // Passing null request has no effect.
  audio_capturer_->BindGainControl(nullptr);

  // Malformed request should also have no effect.
  auto err_handler = [this](zx_status_t error) { error_occurred_ = true; };
  fuchsia::media::AudioCapturerPtr audio_capturer_2;
  audio_->CreateAudioCapturer(audio_capturer_2.NewRequest(), false);
  audio_capturer_2.set_error_handler(err_handler);

  fidl::InterfaceRequest<fuchsia::media::audio::GainControl> bad_request;
  auto bad_request_void_ptr = static_cast<void*>(&bad_request);
  auto bad_request_dword_ptr = static_cast<uint32_t*>(bad_request_void_ptr);
  *bad_request_dword_ptr = 0x0BADCAFE;
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
