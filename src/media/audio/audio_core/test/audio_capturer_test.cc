// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include "src/media/audio/lib/test/audio_core_test_base.h"

namespace media::audio::test {

//
// AudioCapturerTest
//
// This set of tests verifies asynchronous usage of AudioCapturer.
class AudioCapturerTest : public AudioCoreTestBase {
 protected:
  void SetUp() override;
  void TearDown() override;
  void SetNegativeExpectations() override;

  fuchsia::media::AudioCapturerPtr audio_capturer_;
  fuchsia::media::audio::GainControlPtr gain_control_;

  bool bound_capturer_expected_ = true;
};

//
// AudioCapturerTest implementation
//
void AudioCapturerTest::SetUp() {
  AudioCoreTestBase::SetUp();

  audio_core_->CreateAudioCapturer(false, audio_capturer_.NewRequest());
  audio_capturer_.set_error_handler(ErrorHandler());
}

void AudioCapturerTest::TearDown() {
  gain_control_.Unbind();

  EXPECT_EQ(bound_capturer_expected_, audio_capturer_.is_bound());
  audio_capturer_.Unbind();

  AudioCoreTestBase::TearDown();
}

void AudioCapturerTest::SetNegativeExpectations() {
  AudioCoreTestBase::SetNegativeExpectations();
  bound_capturer_expected_ = false;
}

//
// Test cases
//
// AudioCapturer implements the base classes StreamBufferSet and StreamSource.

// StreamBufferSet validation
//

// TODO(mpuryear): test AddPayloadBuffer(uint32 id, handle<vmo> payload_buffer);
// Also negative testing: bad id, null or bad handle

// TODO(mpuryear): test RemovePayloadBuffer(uint32 id);
// Also negative testing: unknown or already-removed id

// TODO(mpuryear): apply same tests to AudioRenderer and AudioCapturer
// (although their implementations within AudioCore differ somewhat).

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
  audio_capturer_->DiscardAllPackets(CompletionCallback());

  ExpectDisconnect();
}

// TODO(mpuryear): DiscardAllPacketsNoReply() when started, post-stop
TEST_F(AudioCapturerTest, DiscardAllNoReplyWithNone) {
  audio_capturer_->DiscardAllPacketsNoReply();

  ExpectDisconnect();
}

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
  audio_capturer_->StopAsyncCapture(CompletionCallback());

  ExpectDisconnect();
}
// Also test before format set, before packets submitted

TEST_F(AudioCapturerTest, StopNoReplyWhenStoppedCausesDisconnect) {
  audio_capturer_->StopAsyncCaptureNoReply();

  ExpectDisconnect();
}
// Also before format set, before packets submitted

// Test creation and interface independence of GainControl.
// In a number of tests below, we run the message loop to give the AudioCapturer
// or GainControl binding a chance to disconnect, if an error occurred.
TEST_F(AudioCapturerTest, BindGainControl) {
  // Validate AudioCapturers can create GainControl interfaces.
  bool capturer_error_occurred = false;
  bool capturer_error_occurred_2 = false;
  bool gain_error_occurred = false;
  bool gain_error_occurred_2 = false;

  audio_capturer_.set_error_handler(
      ErrorHandler([&capturer_error_occurred](zx_status_t) {
        capturer_error_occurred = true;
      }));

  audio_capturer_->BindGainControl(gain_control_.NewRequest());
  gain_control_.set_error_handler(ErrorHandler(
      [&gain_error_occurred](zx_status_t) { gain_error_occurred = true; }));

  fuchsia::media::AudioCapturerPtr audio_capturer_2;
  audio_core_->CreateAudioCapturer(true, audio_capturer_2.NewRequest());
  audio_capturer_2.set_error_handler(
      ErrorHandler([&capturer_error_occurred_2](zx_status_t) {
        capturer_error_occurred_2 = true;
      }));

  fuchsia::media::audio::GainControlPtr gain_control_2;
  audio_capturer_2->BindGainControl(gain_control_2.NewRequest());
  gain_control_2.set_error_handler(ErrorHandler(
      [&gain_error_occurred_2](zx_status_t) { gain_error_occurred_2 = true; }));

  // What happens to a child gain_control, when a capturer is unbound?
  audio_capturer_.Unbind();

  // What happens to a parent capturer, when a gain_control is unbound?
  gain_control_2.Unbind();

  // Give audio_capturer_ a chance to disconnect gain_control_
  ExpectDisconnect();

  // If gain_control_ disconnected as expected, reset errors for the next step.
  if (gain_error_occurred) {
    error_expected_ = false;
    error_occurred_ = false;
  }

  // Give time for other Disconnects to occur, if they must.
  audio_capturer_2->GetStreamType(
      CompletionCallback([](fuchsia::media::StreamType) {}));
  ExpectCallback();

  // Explicitly unbinding audio_capturer_ should disconnect gain_control_.
  EXPECT_FALSE(capturer_error_occurred);
  EXPECT_TRUE(gain_error_occurred);
  EXPECT_FALSE(gain_control_.is_bound());

  // gain_2's parent should NOT disconnect, nor a gain_2 disconnect callback.
  EXPECT_FALSE(capturer_error_occurred_2);
  EXPECT_FALSE(gain_error_occurred_2);
  EXPECT_TRUE(audio_capturer_2.is_bound());
}

// Null requests to BindGainControl should have no effect.
TEST_F(AudioCapturerTest, BindGainControlNull) {
  audio_capturer_->BindGainControl(nullptr);

  // Give time for Disconnect to occur, if it must.
  audio_capturer_->GetStreamType(
      CompletionCallback([](fuchsia::media::StreamType) {}));
  ExpectCallback();
}

// TODO(mpuryear): test GetStreamType() -> (StreamType stream_type);
// Also negative testing: before format set

}  // namespace media::audio::test
