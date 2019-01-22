// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include <lib/gtest/real_loop_fixture.h>

#include "garnet/bin/media/audio_core/test/audio_fidl_tests_shared.h"
#include "lib/component/cpp/environment_services_helper.h"

namespace media {
namespace audio {
namespace test {

//
// AudioRendererTest
//
// This set of tests verifies asynchronous usage of AudioRenderer.
class AudioRendererTest : public gtest::RealLoopFixture {
 protected:
  virtual void SetUp() override;
  void TearDown() override;

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::GainControlPtr gain_control_;

  bool error_occurred_ = false;
  bool expect_error_ = false;
  bool expect_renderer_ = true;
};

//
// AudioRendererTestNegative
//
// A specialization of AudioRendererTest to validate scenarios where we expect
// AudioRenderer bindings to disconnect (Audio bindings should be OK).
class AudioRendererTest_Negative : public AudioRendererTest {
 protected:
  void SetUp() override;
  void ExpectDisconnect();
};

//
// AudioRendererTest implementation
//
void AudioRendererTest::SetUp() {
  ::gtest::RealLoopFixture::SetUp();

  auto err_handler = [this](zx_status_t error) { error_occurred_ = true; };

  environment_services_ = component::GetEnvironmentServices();
  environment_services_->ConnectToService(audio_.NewRequest());
  audio_.set_error_handler(err_handler);

  audio_->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_renderer_.set_error_handler(err_handler);
}

void AudioRendererTest::TearDown() {
  ASSERT_TRUE(audio_.is_bound());
  EXPECT_EQ(expect_error_, error_occurred_);
  EXPECT_EQ(expect_renderer_, audio_renderer_.is_bound());

  ::gtest::RealLoopFixture::TearDown();
}

//
// AudioRendererTest_Negative implementation
//
void AudioRendererTest_Negative::SetUp() {
  AudioRendererTest::SetUp();

  expect_error_ = true;
  expect_renderer_ = false;
}

void AudioRendererTest_Negative::ExpectDisconnect() {
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([this]() { return error_occurred_; },
                                        kDurationResponseExpected,
                                        kDurationGranularity));
}

//
// AudioRenderer validation
//
//
// TODO(mpuryear): Remaining test coverage work within AudioRenderer:
// SetPtsUnits, SetPtsContinuityThreshold, SetReferenceClock;
// Also, positive coverage for Play, PlayNoReply, Pause, PauseNoReply,
//

// AudioRenderer contains an internal state machine. To enter the "configured"
// state, it must receive and successfully execute both SetPcmStreamType and
// SetPayloadBuffer calls. From a Configured state only, it then transitions to
// "operational" mode when any packets are enqueued (received and not yet played
// and/or released).

// TODO(mpuryear): add tests to validate the following --
// **** Basic API validation for asynchronous AudioRenderer:
// SetPayloadBuffer, SendPacket, SendPacketNoReply, Flush.

// **** Before we enter Configured mode:
// SendPacket before SetPcmStreamType must fail.
// SendPacket before SetPayloadBuffer must fail.

// **** While in Configured mode:
// Before SendPacket, all valid SetPayloadBuffer should succeed.

// **** While in Operational mode:
// After SetPcmStreamType+SetPayloadBuffer, valid SendPacket should succeed.
// While renderer Operational, SetPcmStreamType must fail.
// While renderer Operational, SetPayloadBuffer must fail.
// Calling Flush must cancel+return all enqueued (sent) packets.

// **** Once back in Configured (non-Operational) mode
// Flush OR "enqueued packets drain" take renderer out of Operational.
// Once no packets are queued, all valid SetPcmStreamType should succeed.
// Once no packets are queued, all valid SetPayloadBuffer should succeed.
//

// Setting PCM format within known-supportable range of values should succeed.
// Before renderers are operational, multiple SetPcmStreamTypes should succeed.
// We test twice because of previous bug, where the first succeeded but any
// subsequent call (before Play) would cause a FIDL channel disconnect.
TEST_F(AudioRendererTest, SetPcmStreamType) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  audio_renderer_->SetPcmStreamType(std::move(format));

  fuchsia::media::AudioStreamType format2;
  format2.sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
  format2.channels = 1;
  format2.frames_per_second = 44100;
  audio_renderer_->SetPcmStreamType(std::move(format2));

  // Allow an error Disconnect callback, but we expect a timeout instead.
  EXPECT_FALSE(RunLoopWithTimeoutOrUntil([this]() { return error_occurred_; },
                                         kDurationTimeoutExpected))
      << kConnectionErr;
}

// TODO(mpuryear): test SetPtsUnits(uint32 tick_per_sec_num,uint32 denom);

// TODO(mpuryear): test SetPtsContinuityThreshold(float32 threshold_sec);

// TODO(mpuryear): test SetReferenceClock(handle reference_clock);

// TODO(mpuryear): test Play(int64 ref_time, int64 med)->(int64 ref, int64 med);
// Verify success after setting format and submitting buffers.

// TODO(mpuryear): test PlayNoReply(int64 reference_time, int64 media_time);
// Verify success after setting format and submitting buffers.

// TODO(mpuryear): test Pause()->(int64 reference_time, int64 media_time);
// Verify success after setting format and submitting buffers.

// TODO(mpuryear): test PauseNoReply();
// Verify success after setting format and submitting buffers.

// Validate MinLeadTime events, when enabled.
TEST_F(AudioRendererTest, EnableMinLeadTimeEvents) {
  int64_t min_lead_time = -1;
  audio_renderer_.events().OnMinLeadTimeChanged =
      [&min_lead_time](int64_t min_lead_time_nsec) {
        min_lead_time = min_lead_time_nsec;
      };

  audio_renderer_->EnableMinLeadTimeEvents(true);

  // After enabling MinLeadTime events, we expect an initial notification.
  // Because we have not yet set the format, we expect MinLeadTime to be 0.
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this, &min_lead_time]() {
        return error_occurred_ || (min_lead_time >= 0);
      },
      kDurationResponseExpected, kDurationGranularity))
      << kTimeoutErr;

  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_EQ(min_lead_time, 0);

  // FYI: after setting format, MinLeadTime should change to be greater than 0
  // IF the target has AudioOutput devices, or remain 0 (no callback) if it has
  // none. Both are valid possibilities, so we don't test that aspect here.
}

// Validate MinLeadTime events, when disabled.
TEST_F(AudioRendererTest, DisableMinLeadTimeEvents) {
  int64_t min_lead_time = -1;
  audio_renderer_.events().OnMinLeadTimeChanged =
      [&min_lead_time](int64_t min_lead_time_nsec) {
        min_lead_time = min_lead_time_nsec;
      };

  audio_renderer_->EnableMinLeadTimeEvents(false);

  // Callback should not be received (expect loop to timeout? TRUE).
  // If we did, either way it is an error: MinLeadTime event or disconnect.
  EXPECT_FALSE(RunLoopWithTimeoutOrUntil(
      [this, &min_lead_time]() {
        return error_occurred_ || (min_lead_time >= 0);
      },
      kDurationTimeoutExpected));

  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_EQ(min_lead_time, -1) << "Received unexpected MinLeadTime update";
}

//
// Basic validation of GetMinLeadTime() for the asynchronous AudioRenderer.
// Before SetPcmStreamType is called, MinLeadTime should equal zero.
TEST_F(AudioRendererTest, GetMinLeadTime) {
  int64_t min_lead_time = -1;
  audio_renderer_->GetMinLeadTime([&min_lead_time](int64_t min_lead_time_nsec) {
    min_lead_time = min_lead_time_nsec;
  });

  // Wait to receive Lead time callback (will loop timeout? EXPECT_FALSE)
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this, &min_lead_time]() {
        return error_occurred_ || (min_lead_time >= 0);
      },
      kDurationResponseExpected, kDurationGranularity))
      << kTimeoutErr;
  EXPECT_EQ(min_lead_time, 0);
}

// Test creation and interface independence of GainControl.
// In a number of tests below, we run the message loop to give the AudioRenderer
// or GainControl binding a chance to disconnect, if an error occurred.
TEST_F(AudioRendererTest, BindGainControl) {
  // Validate AudioRenderers can create GainControl interfaces.
  audio_renderer_->BindGainControl(gain_control_.NewRequest());
  bool gc_error_occurred = false;
  auto gc_err_handler = [&gc_error_occurred](zx_status_t error) {
    gc_error_occurred = true;
  };
  gain_control_.set_error_handler(gc_err_handler);

  fuchsia::media::AudioRendererPtr audio_renderer_2;
  audio_->CreateAudioRenderer(audio_renderer_2.NewRequest());
  bool ar2_error_occurred = false;
  auto ar2_err_handler = [&ar2_error_occurred](zx_status_t error) {
    ar2_error_occurred = true;
  };
  audio_renderer_2.set_error_handler(ar2_err_handler);

  fuchsia::media::GainControlPtr gain_control_2;
  audio_renderer_2->BindGainControl(gain_control_2.NewRequest());
  bool gc2_error_occurred = false;
  auto gc2_err_handler = [&gc2_error_occurred](zx_status_t error) {
    gc2_error_occurred = true;
  };
  gain_control_2.set_error_handler(gc2_err_handler);

  // Validate GainControl does NOT persist after AudioRenderer is unbound.
  expect_renderer_ = false;
  audio_renderer_.Unbind();

  // Validate that AudioRenderer2 persists without GainControl2.
  gain_control_2.Unbind();

  // ...give interfaces a chance to disconnect...
  EXPECT_FALSE(RunLoopWithTimeoutOrUntil(
      [this, &ar2_error_occurred, &gc2_error_occurred]() {
        return (error_occurred_ || ar2_error_occurred || gc2_error_occurred);
      },
      kDurationTimeoutExpected));

  // Explicitly unbinding audio_renderer_ should not trigger its disconnect
  // (error_occurred_), but should trigger gain_control_'s disconnect.
  EXPECT_FALSE(error_occurred_);
  EXPECT_TRUE(gc_error_occurred);
  EXPECT_FALSE(gain_control_.is_bound());

  // Explicitly unbinding gain_control_2 should not trigger its disconnect, nor
  // its parent audio_renderer_2's.
  EXPECT_FALSE(ar2_error_occurred);
  EXPECT_FALSE(gc2_error_occurred);
  EXPECT_TRUE(audio_renderer_2.is_bound());
}

//
// AudioRendererTest_Negative
//
// Separate test class for cases in which we expect the AudioRenderer binding to
// disconnect, and our AudioRenderer interface ptr to be reset.
//
// SetStreamType is not yet implemented and expected to cause a Disconnect.
TEST_F(AudioRendererTest_Negative, SetStreamType) {
  fuchsia::media::AudioStreamType stream_format;
  stream_format.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  stream_format.channels = 1;
  stream_format.frames_per_second = 8000;

  fuchsia::media::StreamType stream_type;
  stream_type.encoding = fuchsia::media::AUDIO_ENCODING_LPCM;
  stream_type.medium_specific.set_audio(stream_format);

  audio_renderer_->SetStreamType(std::move(stream_type));

  // Binding should Disconnect (EXPECT loop to NOT timeout)
  ExpectDisconnect();
}

// TODO(mpuryear): negative tests for the following:
//    SetPtsUnits(uint32 tick_per_sec_num,uint32 denom)
//    SetPtsContinuityThreshold(float32 threshold_sec)
//    SetReferenceClock(handle reference_clock)

// Before setting format, Play should not succeed.
TEST_F(AudioRendererTest_Negative, PlayWithoutFormat) {
  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP,
                        fuchsia::media::NO_TIMESTAMP,
                        [this, &ref_time_received, &media_time_received](
                            int64_t ref_time, int64_t media_time) {
                          ref_time_received = ref_time;
                          media_time_received = media_time;
                        });

  // Disconnect callback should be received
  ExpectDisconnect();
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// After setting format but before submitting buffers, Play should not succeed.
TEST_F(AudioRendererTest_Negative, PlayWithoutBuffers) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 1;
  format.frames_per_second = 32000;
  audio_renderer_->SetPcmStreamType(std::move(format));

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP,
                        fuchsia::media::NO_TIMESTAMP,
                        [this, &ref_time_received, &media_time_received](
                            int64_t ref_time, int64_t media_time) {
                          ref_time_received = ref_time;
                          media_time_received = media_time;
                        });

  // Disconnect callback should be received
  ExpectDisconnect();
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// Before setting format, PlayNoReply should cause a Disconnect.
TEST_F(AudioRendererTest_Negative, PlayNoReplyWithoutFormat) {
  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP,
                               fuchsia::media::NO_TIMESTAMP);

  // Disconnect callback should be received.
  ExpectDisconnect();
}

// Before setting format, Pause should not succeed.
TEST_F(AudioRendererTest_Negative, PauseWithoutFormat) {
  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Pause([this, &ref_time_received, &media_time_received](
                             int64_t ref_time, int64_t media_time) {
    ref_time_received = ref_time;
    media_time_received = media_time;
  });

  // Disconnect callback should be received
  ExpectDisconnect();
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// After setting format but before submitting buffers, Pause should not succeed.
TEST_F(AudioRendererTest_Negative, PauseWithoutBuffers) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 1;
  format.frames_per_second = 32000;
  audio_renderer_->SetPcmStreamType(std::move(format));

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Pause([this, &ref_time_received, &media_time_received](
                             int64_t ref_time, int64_t media_time) {
    ref_time_received = ref_time;
    media_time_received = media_time;
  });

  // Disconnect callback should be received
  ExpectDisconnect();
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// Before setting format, PauseNoReply should cause a Disconnect.
TEST_F(AudioRendererTest_Negative, PauseNoReplyWithoutFormat) {
  audio_renderer_->PauseNoReply();

  // Disconnect callback should be received.
  ExpectDisconnect();
}

}  // namespace test
}  // namespace audio
}  // namespace media
