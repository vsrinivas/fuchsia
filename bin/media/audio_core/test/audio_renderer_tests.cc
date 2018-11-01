// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/gtest/real_loop_fixture.h>

#include "garnet/bin/media/audio_core/test/audio_core_tests_shared.h"
#include "lib/component/cpp/environment_services_helper.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

//
// AudioRenderer tests
//
//
// TODO(mpuryear): Remaining test coverage work within AudioRenderer:
// SetPtsUnits, SetPtsContinuityThreshold, SetReferenceClock;
// Also, positive coverage for Play, PlayNoReply, Pause, PauseNoReply,
//
// This set of tests verifies asynchronous usage of AudioRenderer.
class AudioRendererTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    ::gtest::RealLoopFixture::SetUp();

    environment_services_ = component::GetEnvironmentServices();
    environment_services_->ConnectToService(audio_.NewRequest());
    ASSERT_TRUE(audio_);

    audio_.set_error_handler([this](zx_status_t status) {
      error_occurred_ = true;
      QuitLoop();
    });

    audio_->CreateAudioRenderer(audio_renderer_.NewRequest());
    ASSERT_TRUE(audio_renderer_);

    audio_renderer_.set_error_handler([this](zx_status_t status) {
      error_occurred_ = true;
      QuitLoop();
    });
  }

  void TearDown() override {
    EXPECT_FALSE(error_occurred_);

    audio_renderer_.Unbind();
    audio_.Unbind();

    ::gtest::RealLoopFixture::TearDown();
  }

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::GainControlPtr gain_control_;

  bool error_occurred_ = false;
};

// Slight specialization, for test cases that expect the binding to disconnect.
class AudioRendererTest_Negative : public AudioRendererTest {
 protected:
  void TearDown() override {
    EXPECT_FALSE(audio_renderer_);
    EXPECT_TRUE(error_occurred_);

    ::gtest::RealLoopFixture::TearDown();
  }
};

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

  ASSERT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));

  fuchsia::media::AudioStreamType format2;
  format2.sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
  format2.channels = 1;
  format2.frames_per_second = 44100;
  audio_renderer_->SetPcmStreamType(std::move(format2));

  // Allow an error Disconnect callback, but we expect a timeout instead.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
}

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
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
}

// TODO(mpuryear): test SetPtsUnits(uint32 tick_per_sec_num,uint32 denom);

// TODO(mpuryear): test SetPtsContinuityThreshold(float32 threshold_sec);

// TODO(mpuryear): test SetReferenceClock(handle reference_clock);

// TODO(mpuryear): test Play(int64 ref_time, int64 med)->(int64 ref, int64 med);
// Verify success after setting format and submitting buffers.

// Before setting format, Play should not succeed.
TEST_F(AudioRendererTest_Negative, PlayNoFormat) {
  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP,
                        fuchsia::media::NO_TIMESTAMP,
                        [this, &ref_time_received, &media_time_received](
                            int64_t ref_time, int64_t media_time) {
                          ref_time_received = ref_time;
                          media_time_received = media_time;
                          QuitLoop();
                        });

  // Disconnect callback should be received
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// After setting format but before submitting buffers, Play should not succeed.
TEST_F(AudioRendererTest_Negative, PlayNoBuffers) {
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
                          QuitLoop();
                        });

  // Disconnect callback should be received
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// TODO(mpuryear): test PlayNoReply(int64 reference_time, int64 media_time);
// Verify success after setting format and submitting buffers.

// Before setting format, PlayNoReply should cause a Disconnect.
TEST_F(AudioRendererTest_Negative, PlayNoReplyNoFormat) {
  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP,
                               fuchsia::media::NO_TIMESTAMP);

  // Disconnect callback should be received.
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
}

// Before setting format, Pause should not succeed.
TEST_F(AudioRendererTest_Negative, PauseNoFormat) {
  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Pause([this, &ref_time_received, &media_time_received](
                             int64_t ref_time, int64_t media_time) {
    ref_time_received = ref_time;
    media_time_received = media_time;
    QuitLoop();
  });

  // Disconnect callback should be received
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// TODO(mpuryear): test Pause()->(int64 reference_time, int64 media_time);
// Verify success after setting format and submitting buffers.

// After setting format but before submitting buffers, Pause should not succeed.
TEST_F(AudioRendererTest_Negative, PauseNoBuffers) {
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
    QuitLoop();
  });

  // Disconnect callback should be received
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// TODO(mpuryear): test PauseNoReply();
// Verify success after setting format and submitting buffers.

// Before setting format, PauseNoReply should cause a Disconnect.
TEST_F(AudioRendererTest_Negative, PauseNoReplyNoFormat) {
  audio_renderer_->PauseNoReply();

  // Disconnect callback should be received.
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
}

// Validate MinLeadTime events, when enabled.
TEST_F(AudioRendererTest, EnableMinLeadTimeEvents) {
  int64_t min_lead_time = -1;
  audio_renderer_.events().OnMinLeadTimeChanged =
      [this, &min_lead_time](int64_t min_lead_time_nsec) {
        min_lead_time = min_lead_time_nsec;
        QuitLoop();
      };

  audio_renderer_->EnableMinLeadTimeEvents(true);

  // We expect to receive the event (no timeout) with value 0.
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(min_lead_time, 0);

  // FYI: after setting format, MinLeadTime > 0 IF we have devices. Otherwise it
  // remains 0 (no callback). Both are valid, so we don't test that aspect here.
}

// Validate MinLeadTime events, when disabled.
TEST_F(AudioRendererTest, DisableMinLeadTimeEvents) {
  int64_t min_lead_time = -1;
  audio_renderer_.events().OnMinLeadTimeChanged =
      [this, &min_lead_time](int64_t min_lead_time_nsec) {
        min_lead_time = min_lead_time_nsec;
        QuitLoop();
      };

  audio_renderer_->EnableMinLeadTimeEvents(false);

  // Callback should not be received (expect loop to timeout? TRUE)
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  EXPECT_EQ(min_lead_time, -1);
}

//
// Basic validation of GetMinLeadTime() for the asynchronous AudioRenderer.
// Before SetPcmStreamType is called, MinLeadTime should equal zero.
TEST_F(AudioRendererTest, GetMinLeadTime) {
  int64_t min_lead_time = -1;
  audio_renderer_->GetMinLeadTime(
      [this, &min_lead_time](int64_t min_lead_time_nsec) {
        min_lead_time = min_lead_time_nsec;
        QuitLoop();
      });

  // Wait to receive Lead time callback (will loop timeout? EXPECT_FALSE)
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_EQ(min_lead_time, 0);
}

// Test creation and interface independence of GainControl.
TEST_F(AudioRendererTest, BindGainControl) {
  // Validate AudioRenderer can create GainControl interface.
  audio_renderer_->BindGainControl(gain_control_.NewRequest());
  // Give AudioRenderer interface a chance to disconnect if it must.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  EXPECT_TRUE(gain_control_);
  EXPECT_TRUE(audio_renderer_);

  // Validate that AudioRenderer persists without GainControl.
  gain_control_.Unbind();
  EXPECT_FALSE(gain_control_);
  // Give AudioRenderer interface a chance to disconnect if it must.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  EXPECT_TRUE(audio_renderer_);

  // Validate GainControl persists after AudioRenderer is unbound.
  audio_renderer_->BindGainControl(gain_control_.NewRequest());
  audio_renderer_.Unbind();
  EXPECT_FALSE(audio_renderer_);
  // At this point, the GainControl may still exist, but...
  EXPECT_TRUE(gain_control_);

  // ...give GainControl interface a chance to disconnect if it must...
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  // ... and by now, it should be gone.
  EXPECT_FALSE(gain_control_);
}

//
// AudioRendererSync tests
//
// Base class for tests of the synchronous AudioRendererSync interface.
// We expect the async and sync interfaces to track each other exactly -- any
// behavior otherwise is a bug in core FIDL. These tests were only created to
// better understand how errors manifest themselves when using sync interfaces.
//
// In short, further testing of the sync interfaces (over and above any testing
// done on the async interfaces) should not be needed.
class AudioRendererSyncTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    ::gtest::RealLoopFixture::SetUp();

    environment_services_ = component::GetEnvironmentServices();
    environment_services_->ConnectToService(audio_.NewRequest());
    ASSERT_TRUE(audio_);

    ASSERT_EQ(ZX_OK, audio_->CreateAudioRenderer(audio_renderer_.NewRequest()));
    ASSERT_TRUE(audio_renderer_);
  }

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::media::AudioSyncPtr audio_;
  fuchsia::media::AudioRendererSyncPtr audio_renderer_;
};

// Basic validation of GetMinLeadTime() for the synchronous AudioRenderer.
// In subsequent synchronous-interface test(s), receiving a valid return value
// from this call is our only way of verifying that the connection survived.
TEST_F(AudioRendererSyncTest, GetMinLeadTime) {
  int64_t min_lead_time = -1;
  ASSERT_EQ(ZX_OK, audio_renderer_->GetMinLeadTime(&min_lead_time));
  EXPECT_GE(min_lead_time, 0);
}

// Before renderers are operational, multiple SetPcmStreamTypes should succeed.
// We test twice because of previous bug, where the first succeeded but any
// subsequent call (before Play) would cause a FIDL channel disconnect.
// GetMinLeadTime is our way of verifying whether the connection survived.
TEST_F(AudioRendererSyncTest, SetPcmFormat_Double) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  EXPECT_EQ(ZX_OK, audio_renderer_->SetPcmStreamType(std::move(format)));

  int64_t min_lead_time = -1;
  EXPECT_EQ(ZX_OK, audio_renderer_->GetMinLeadTime(&min_lead_time));
  EXPECT_GE(min_lead_time, 0);

  fuchsia::media::AudioStreamType format2;
  format2.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  format2.channels = 1;
  format2.frames_per_second = 44100;
  EXPECT_EQ(ZX_OK, audio_renderer_->SetPcmStreamType(std::move(format2)));

  min_lead_time = -1;
  EXPECT_EQ(ZX_OK, audio_renderer_->GetMinLeadTime(&min_lead_time));
  EXPECT_GE(min_lead_time, 0);
}

// Before setting format, PlayNoReply should cause a Disconnect.
// GetMinLeadTime is our way of verifying whether the connection survived.
TEST_F(AudioRendererSyncTest, PlayNoReplyNoFormat) {
  EXPECT_EQ(ZX_OK, audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP,
                                                fuchsia::media::NO_TIMESTAMP));

  int64_t min_lead_time = -1;
  EXPECT_EQ(ZX_ERR_PEER_CLOSED,
            audio_renderer_->GetMinLeadTime(&min_lead_time));
  // Although the connection has disconnected, the proxy should still exist.
  EXPECT_TRUE(audio_renderer_);
}

}  // namespace test
}  // namespace audio
}  // namespace media
