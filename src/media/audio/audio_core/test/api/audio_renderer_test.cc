// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmo.h>

#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

// Just an arbitrary |AudioStreamType| that is valid to be used. Intended for
// tests that don't care about the specific audio frames being sent.
constexpr fuchsia::media::AudioStreamType kTestStreamType = {
    .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
    .channels = 2,
    .frames_per_second = 48000,
};

// The following are valid/invalid when used with |kTestStreamType|.
constexpr uint64_t kValidPayloadSize = sizeof(float) * kTestStreamType.channels;
constexpr uint64_t kInvalidPayloadSize = kValidPayloadSize - 1;
constexpr size_t kDefaultPayloadBufferSize = PAGE_SIZE;

//
// AudioRendererTest
//
// This set of tests verifies asynchronous usage of AudioRenderer.
class AudioRendererTest : public HermeticAudioTest {
 protected:
  void SetUp() override;
  void TearDown() override;

  // Discards all in-flight packets and waits for the response from the audio
  // renderer. This can be used as a simple round-trip through the audio
  // renderer, indicating that all FIDL messages have been read out of the
  // channel.
  //
  // In other words, calling this method also asserts that all prior FIDL
  // messages have been handled successfully (no disconnect was triggered).
  void AssertConnectedAndDiscardAllPackets();

  // Creates a VMO with |buffer_size| and then passes it to
  // |AudioRenderer::AddPayloadBuffer| with |id|. This is purely a convenience
  // method and doesn't provide access to the buffer VMO.
  void CreateAndAddPayloadBuffer(uint32_t id);

  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::audio::GainControlPtr gain_control_;
};

// AudioRendererClockTest - thin wrapper around AudioRendererTest
class AudioRendererClockTest : public AudioRendererTest {
 protected:
  // The clock received from GetRefClock is read-only, but the original can still be adjusted.
  static constexpr auto kClockRights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ;

  zx::clock GetAndValidateReferenceClock() {
    zx::clock clock;

    audio_renderer_->GetReferenceClock(
        AddCallback("GetReferenceClock",
                    [&clock](zx::clock received_clock) { clock = std::move(received_clock); }));

    ExpectCallback();
    EXPECT_TRUE(clock.is_valid());

    return clock;
  }
};

//
// AudioRendererTest implementation
//
void AudioRendererTest::SetUp() {
  HermeticAudioTest::SetUp();

  audio_core_->CreateAudioRenderer(audio_renderer_.NewRequest());
  AddErrorHandler(audio_renderer_, "AudioRenderer");
}

void AudioRendererTest::TearDown() {
  gain_control_.Unbind();
  audio_renderer_.Unbind();

  HermeticAudioTest::TearDown();
}

void AudioRendererTest::AssertConnectedAndDiscardAllPackets() {
  audio_renderer_->DiscardAllPackets(AddCallback("DiscardAllPackets"));

  ExpectCallback();
}

void AudioRendererTest::CreateAndAddPayloadBuffer(uint32_t id) {
  zx::vmo payload_buffer;
  constexpr uint32_t kVmoOptionsNone = 0;
  ASSERT_EQ(zx::vmo::create(kDefaultPayloadBufferSize, kVmoOptionsNone, &payload_buffer), ZX_OK);
  audio_renderer_->AddPayloadBuffer(id, std::move(payload_buffer));
}

//
// AudioRenderer implements the base classes StreamBufferSet and StreamSink.

//
// StreamBufferSet validation
//
// Sanity test adding a payload buffer. Just verify we don't get a disconnect.
TEST_F(AudioRendererTest, AddPayloadBuffer) {
  CreateAndAddPayloadBuffer(0);
  CreateAndAddPayloadBuffer(1);
  CreateAndAddPayloadBuffer(2);

  AssertConnectedAndDiscardAllPackets();
}

// TODO(tjdetwiler): This is out of spec but there are currently clients that
// rely on this behavior. This test should be updated to fail once all clients
// are fixed.
TEST_F(AudioRendererTest, AddPayloadBuffer_DuplicateId) {
  CreateAndAddPayloadBuffer(0);
  CreateAndAddPayloadBuffer(0);

  AssertConnectedAndDiscardAllPackets();
}

// It is invalid to add a payload buffer while there are queued packets.
TEST_F(AudioRendererTest, AddPayloadBuffer_WhileOperationalShouldDisconnect) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  AssertConnectedAndDiscardAllPackets();

  // Send Packet moves connection into the operational state.
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));

  // Attempt to add new payload buffer while the packet is in flight. This
  // should fail.
  CreateAndAddPayloadBuffer(0);

  ExpectDisconnect(audio_renderer_);
}

// Test removing payload buffers.
TEST_F(AudioRendererTest, RemovePayloadBuffer) {
  CreateAndAddPayloadBuffer(0);
  CreateAndAddPayloadBuffer(1);
  CreateAndAddPayloadBuffer(2);
  audio_renderer_->RemovePayloadBuffer(0);
  audio_renderer_->RemovePayloadBuffer(1);
  audio_renderer_->RemovePayloadBuffer(2);

  AssertConnectedAndDiscardAllPackets();
}

// Test RemovePayloadBuffer with an invalid ID (does not have a corresponding
// AddPayloadBuffer).
TEST_F(AudioRendererTest, RemovePayloadBuffer_InvalidBufferIdShouldDisconnect) {
  audio_renderer_->RemovePayloadBuffer(0);

  ExpectDisconnect(audio_renderer_);
}

// It is invalid to remove a payload buffer while there are queued packets.
TEST_F(AudioRendererTest, RemovePayloadBuffer_WhileOperationalShouldDisconnect) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  AssertConnectedAndDiscardAllPackets();

  // Send Packet moves connection into the operational state.
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));

  // Attempt to add new payload buffer while the packet is in flight. This
  // should fail.
  audio_renderer_->RemovePayloadBuffer(0);

  ExpectDisconnect(audio_renderer_);
}

//
// StreamSink validation
//

//
// SendPacket tests.
//
TEST_F(AudioRendererTest, SendPacket) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send a packet (we don't care about the actual packet data here).
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacket(std::move(packet), AddCallback("SendPacket"));

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                        [](int64_t, int64_t) {});
  ExpectCallback();
}

TEST_F(AudioRendererTest, SendPacket_InvokesCallbacksInOrder) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send a packet (we don't care about the actual packet data here).
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacket(fidl::Clone(packet), AddCallback("SendPacket1"));
  audio_renderer_->SendPacket(fidl::Clone(packet), AddCallback("SendPacket2"));
  audio_renderer_->SendPacket(fidl::Clone(packet), AddCallback("SendPacket3"));
  audio_renderer_->SendPacket(fidl::Clone(packet), AddCallback("SendPacket4"));

  // Play and expect the callbacks in order.
  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                        [](int64_t, int64_t) {});
  ExpectCallback();
}

TEST_F(AudioRendererTest, SendPackets_TooManyShouldDisconnect) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send a packet (we don't care about the actual packet data here).
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;

  // The exact limit is a function of the size of some internal data structures. We verify this
  // limit is somewhere between 500 and 600 packets.
  for (int i = 0; i < 500; ++i) {
    audio_renderer_->SendPacketNoReply(std::move(packet));
  }
  AssertConnectedAndDiscardAllPackets();

  for (int i = 0; i < 600; ++i) {
    audio_renderer_->SendPacketNoReply(std::move(packet));
  }
  ExpectDisconnect(audio_renderer_);
}

//
// SendPacketNoReply tests.
//

TEST_F(AudioRendererTest, SendPacketNoReply) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send a packet (we don't care about the actual packet data here).
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));

  AssertConnectedAndDiscardAllPackets();
}

TEST_F(AudioRendererTest, SendPacketNoReply_InvalidPayloadBufferIdShouldDisconnect) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send a packet (we don't care about the actual packet data here).
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 1234;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));

  ExpectDisconnect(audio_renderer_);
}

// It is invalid to SendPacket before the stream type has been configured
// (SetPcmStreamType).
TEST_F(AudioRendererTest, SendPacketNoReply_BeforeSetPcmStreamTypeShouldDisconnect) {
  // Add a payload buffer but no stream type.
  CreateAndAddPayloadBuffer(0);

  // SendPacket. This should trigger a disconnect due to a lack of a configured
  // stream type.
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));

  ExpectDisconnect(audio_renderer_);
}

// SendPacket with a |payload_size| that is invalid.
TEST_F(AudioRendererTest, SendPacketNoReply_InvalidPayloadBufferSizeShouldDisconnect) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send Packet moves connection into the operational state.
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kInvalidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));

  ExpectDisconnect(audio_renderer_);
}

TEST_F(AudioRendererTest, SendPacketNoReply_BufferOutOfBoundsShouldDisconnect) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send Packet moves connection into the operational state.
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  // |payload_offset| is beyond the end of the payload buffer.
  packet.payload_offset = kDefaultPayloadBufferSize;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));

  ExpectDisconnect(audio_renderer_);
}

TEST_F(AudioRendererTest, SendPacketNoReply_BufferOverrunShouldDisconnect) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send Packet moves connection into the operational state.
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  // |payload_offset| + |payload_size| is beyond the end of the payload buffer.
  packet.payload_size = kValidPayloadSize * 2;
  packet.payload_offset = kDefaultPayloadBufferSize - kValidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));

  ExpectDisconnect(audio_renderer_);
}

// TODO(mpuryear): test EndOfStream();
// Also proper sequence of callbacks/completions

TEST_F(AudioRendererTest, DiscardAllPackets_BeforeConfiguredDoesntComputeTimeline) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->DiscardAllPacketsNoReply();

  int64_t play_ref_time = -1, play_media_time = -1;
  int64_t pause_ref_time = -1, pause_media_time = -1;

  audio_renderer_->Play(
      fuchsia::media::NO_TIMESTAMP, 0,
      AddCallback("Play", [&play_ref_time, &play_media_time](auto ref_time, auto media_time) {
        play_ref_time = ref_time;
        play_media_time = media_time;
      }));

  ExpectCallback();

  // If we call Play(NO_TIMESTAMP) and then Pause immediately, it is possible for pause_ref_time <
  // play_ref_time.  Even in the NO_TIMESTAMP case, audio_core still applies some small amount of
  // padding in order to guarantee that we can start exactly when we said we would.
  //
  // If pause_ref_time IS less than play_ref_time, then the equivalent pause_media_time would be
  // negative.  We shouldn't fail in that case, but instead let's avoid the entire problem by doing
  // this
  zx_nanosleep(play_ref_time);

  audio_renderer_->Pause(
      AddCallback("Pause", [&pause_ref_time, &pause_media_time](auto ref_time, auto media_time) {
        pause_ref_time = ref_time;
        pause_media_time = media_time;
      }));

  ExpectCallback();

  EXPECT_GE(pause_ref_time, play_ref_time);

  // the media time returned from pause is calculated from the audio renderers timeline function.
  // This ensures that calling Discard before Play/Pause doesn't prevent the timeline from making
  // forward progress.
  if (pause_ref_time > play_ref_time) {
    EXPECT_GT(pause_media_time, 0);
  } else {
    EXPECT_EQ(pause_media_time, 0);
  }
}

// DiscardAllPackets waits to deliver its completion callback until all packets have returned.
TEST_F(AudioRendererTest, DiscardAllPackets_ReturnsAfterAllPackets) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  AssertConnectedAndDiscardAllPackets();

  // Even if one packet completes almost immediately, the others will still be outstanding.
  fuchsia::media::StreamPacket packet1, packet2, packet3;
  packet1.payload_buffer_id = packet2.payload_buffer_id = packet3.payload_buffer_id = 0;
  packet1.payload_offset = packet2.payload_offset = packet3.payload_offset = 0;
  packet1.payload_size = packet2.payload_size = packet3.payload_size = kDefaultPayloadBufferSize;

  audio_renderer_->SendPacket(std::move(packet1), AddCallback("SendPacket1"));
  audio_renderer_->SendPacket(std::move(packet2), AddCallback("SendPacket2"));
  audio_renderer_->SendPacket(std::move(packet3), AddCallback("SendPacket3"));
  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);

  // Packets must complete in order, with the DiscardAllPackets completion afterward.
  audio_renderer_->DiscardAllPackets(AddCallback("DiscardAllPackets"));
  ExpectCallback();
}

// TODO(mpuryear): test DiscardAllPacketsNoReply();
// Also when no packets, when started

//
// AudioRenderer validation
//

// AudioRenderer contains an internal state machine. To enter the "configured"
// state, it must receive and successfully execute both SetPcmStreamType and
// SetPayloadBuffer calls. From a Configured state only, it then transitions to
// "operational" mode when any packets are enqueued (received and not yet played
// and/or released).

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
  audio_renderer_->SetPcmStreamType(format);

  fuchsia::media::AudioStreamType format2;
  format2.sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
  format2.channels = 1;
  format2.frames_per_second = 44100;
  audio_renderer_->SetPcmStreamType(format2);

  // Allow an error Disconnect callback, but we expect a timeout instead.
  audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime"));
  ExpectCallback();
}

// TODO(mpuryear): test SetPtsUnits(uint32 tick_per_sec_num,uint32 denom);
// Also negative testing: zero values, nullptrs, huge num/small denom

// TODO(mpuryear): test SetPtsContinuityThreshold(float32 threshold_sec);
// Also negative testing: NaN, negative, very large, infinity

TEST_F(AudioRendererTest, Play) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send a packet (we don't care about the actual packet data here).
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacket(std::move(packet), AddCallback("SendPacket"));

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;
  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                        [&ref_time_received, &media_time_received](auto ref_time, auto media_time) {
                          ref_time_received = ref_time;
                          media_time_received = media_time;
                        });
  // Note we expect that we receive the |Play| callback _before_ the
  // |SendPacket| callback.
  ExpectCallback();
  ASSERT_NE(ref_time_received, -1);
  ASSERT_NE(media_time_received, -1);
}

// TODO(mpuryear): Also: when already in Play, very positive vals, very negative
// vals
TEST_F(AudioRendererTest, PlayNoReply) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send a packet (we don't care about the actual packet data here).
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacket(std::move(packet), AddCallback("SendPacket"));

  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);
  ExpectCallback();
}

// TODO(mpuryear): test Pause()->(int64 reference_time, int64 media_time);
// Verify success after setting format and submitting buffers.
// Also: when already in Pause

// TODO(mpuryear): test PauseNoReply();
// Verify success after setting format and submitting buffers.
// Also: when already in Pause

// Validate MinLeadTime events, when enabled.
TEST_F(AudioRendererTest, EnableMinLeadTimeEvents) {
  int64_t min_lead_time = -1;
  audio_renderer_.events().OnMinLeadTimeChanged = AddCallback(
      "OnMinLeadTimeChanged",
      [&min_lead_time](int64_t min_lead_time_nsec) { min_lead_time = min_lead_time_nsec; });

  audio_renderer_->EnableMinLeadTimeEvents(true);

  // After enabling MinLeadTime events, we expect an initial notification.
  // Because we have not yet set the format, we expect MinLeadTime to be 0.
  ExpectCallback();
  EXPECT_EQ(min_lead_time, 0);

  // FYI: after setting format, MinLeadTime should change to be greater than 0
  // IF the target has AudioOutput devices, or remain 0 (no callback) if it has
  // none. Both are valid possibilities, so we don't test that aspect here.
}

// Validate MinLeadTime events, when disabled.
TEST_F(AudioRendererTest, DisableMinLeadTimeEvents) {
  audio_renderer_.events().OnMinLeadTimeChanged = [](int64_t x) {
    ADD_FAILURE() << "Unexpected call to OnMinLeadTimeChanged";
  };

  audio_renderer_->EnableMinLeadTimeEvents(false);

  // We should not receive a OnMinLeadTimeChanged callback (or Disconnect)
  // before receiving this direct GetMinLeadTime callback.
  audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime"));
  ExpectCallback();
}

//
// Basic validation of GetMinLeadTime() for the asynchronous AudioRenderer.
// Before SetPcmStreamType is called, MinLeadTime should equal zero.
TEST_F(AudioRendererTest, GetMinLeadTime) {
  int64_t min_lead_time = -1;
  audio_renderer_->GetMinLeadTime(AddCallback(
      "GetMinLeadTime",
      [&min_lead_time](int64_t min_lead_time_nsec) { min_lead_time = min_lead_time_nsec; }));

  // Wait to receive Lead time callback (will loop timeout? EXPECT_FALSE)
  ExpectCallback();
  EXPECT_EQ(min_lead_time, 0);
}

// Test creation and interface independence of GainControl.
// In a number of tests below, we run the message loop to give the AudioRenderer
// or GainControl binding a chance to disconnect, if an error occurred.
TEST_F(AudioRendererTest, BindGainControl) {
  // Validate AudioRenderers can create GainControl interfaces.
  audio_renderer_->BindGainControl(gain_control_.NewRequest());
  AddErrorHandler(gain_control_, "AudioRenderer::GainControl");

  fuchsia::media::AudioRendererPtr audio_renderer_2;
  audio_core_->CreateAudioRenderer(audio_renderer_2.NewRequest());
  AddErrorHandler(audio_renderer_2, "AudioRenderer2");

  fuchsia::media::audio::GainControlPtr gain_control_2;
  audio_renderer_2->BindGainControl(gain_control_2.NewRequest());
  AddErrorHandler(gain_control_2, "AudioRenderer::GainControl2");

  // Validate GainControl2 does NOT persist after audio_renderer_2 is unbound
  audio_renderer_2.Unbind();

  // Validate that audio_renderer_ persists without gain_control_
  gain_control_.Unbind();

  // Give audio_renderer_2 a chance to disconnect gain_control_2
  ExpectDisconnect(gain_control_2);

  // Let audio_renderer_ show it is still alive (and allow other disconnects)
  audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime"));
  ExpectCallback();
}

// Before setting format, Play should not succeed.
TEST_F(AudioRendererTest, Play_WithoutFormatShouldDisconnect) {
  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Play(
      fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
      [&ref_time_received, &media_time_received](int64_t ref_time, int64_t media_time) {
        ref_time_received = ref_time;
        media_time_received = media_time;
      });

  // Disconnect callback should be received
  ExpectDisconnect(audio_renderer_);
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// After setting format but before submitting buffers, Play should not succeed.
TEST_F(AudioRendererTest, Play_WithoutBuffersShouldDisconnect) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 1;
  format.frames_per_second = 32000;
  audio_renderer_->SetPcmStreamType(format);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Play(
      fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
      [&ref_time_received, &media_time_received](int64_t ref_time, int64_t media_time) {
        ref_time_received = ref_time;
        media_time_received = media_time;
      });

  // Disconnect callback should be received
  ExpectDisconnect(audio_renderer_);
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// Before setting format, PlayNoReply should cause a Disconnect.
TEST_F(AudioRendererTest, PlayNoReply_WithoutFormatShouldDisconnect) {
  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);

  // Disconnect callback should be received.
  ExpectDisconnect(audio_renderer_);
}

// Before setting format, Pause should not succeed.
TEST_F(AudioRendererTest, PauseWithoutFormatShouldDisconnect) {
  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Pause(
      [&ref_time_received, &media_time_received](int64_t ref_time, int64_t media_time) {
        ref_time_received = ref_time;
        media_time_received = media_time;
      });

  // Disconnect callback should be received
  ExpectDisconnect(audio_renderer_);
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// After setting format but before submitting buffers, Pause should not succeed.
TEST_F(AudioRendererTest, Pause_WithoutBuffersShouldDisconnect) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 1;
  format.frames_per_second = 32000;
  audio_renderer_->SetPcmStreamType(format);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Pause(
      [&ref_time_received, &media_time_received](int64_t ref_time, int64_t media_time) {
        ref_time_received = ref_time;
        media_time_received = media_time;
      });

  // Disconnect callback should be received
  ExpectDisconnect(audio_renderer_);
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// Before setting format, PauseNoReply should cause a Disconnect.
TEST_F(AudioRendererTest, PauseNoReply_WithoutFormatShouldDisconnect) {
  audio_renderer_->PauseNoReply();

  // Disconnect callback should be received.
  ExpectDisconnect(audio_renderer_);
}

TEST_F(AudioRendererTest, SetUsage_AfterSetPcmStreamTypeShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  AssertConnectedAndDiscardAllPackets();

  audio_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION);
  ExpectDisconnect(audio_renderer_);
}

//
// AudioRenderer reference clock methods
//

// Accept the default clock that is returned if we set no clock
TEST_F(AudioRendererClockTest, SetRefClock_Default) {
  zx::clock ref_clock = GetAndValidateReferenceClock();

  clock::testing::VerifyReadOnlyRights(ref_clock);
  clock::testing::VerifyIsSystemMonotonic(ref_clock);

  clock::testing::VerifyAdvances(ref_clock);
  clock::testing::VerifyCannotBeRateAdjusted(ref_clock);
}

// Set a null clock; this represents selecting the AudioCore-generated clock.
TEST_F(AudioRendererClockTest, SetRefClock_Flexible) {
  audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));
  zx::clock provided_clock = GetAndValidateReferenceClock();

  clock::testing::VerifyReadOnlyRights(provided_clock);
  clock::testing::VerifyIsSystemMonotonic(provided_clock);

  clock::testing::VerifyAdvances(provided_clock);
  clock::testing::VerifyCannotBeRateAdjusted(provided_clock);
}

// Set a recognizable custom reference clock -- should be what we receive from GetReferenceClock.
// Also, the clock received from GetRefClock is read-only, but the original can still be adjusted.
TEST_F(AudioRendererClockTest, SetRefClock_Custom) {
  // Set a recognizable custom reference clock -- should be what we receive from GetReferenceClock.
  zx::clock dupe_clock, retained_clock, orig_clock = clock::AdjustableCloneOfMonotonic();
  zx::clock::update_args args;
  args.reset().set_rate_adjust(-100);
  ASSERT_EQ(orig_clock.update(args), ZX_OK) << "clock.update with rate_adjust failed";

  ASSERT_EQ(orig_clock.duplicate(kClockRights, &dupe_clock), ZX_OK);
  ASSERT_EQ(orig_clock.duplicate(kClockRights, &retained_clock), ZX_OK);

  audio_renderer_->SetReferenceClock(std::move(dupe_clock));
  zx::clock received_clock = GetAndValidateReferenceClock();

  clock::testing::VerifyReadOnlyRights(received_clock);
  clock::testing::VerifyIsNotSystemMonotonic(received_clock);

  clock::testing::VerifyAdvances(received_clock);
  clock::testing::VerifyCannotBeRateAdjusted(received_clock);

  // We can still rate-adjust our custom clock.
  clock::testing::VerifyCanBeRateAdjusted(orig_clock);
  clock::testing::VerifyAdvances(orig_clock);
}

// inadequate ZX_RIGHTS -- if no TRANSFER, the SetReferenceClock silently does nothing.
// The reference clock should remain the unique recognizable reference clock from before the call.
TEST_F(AudioRendererClockTest, SetRefClock_NoTransferNoChange) {
  // First create a unique custom clock that we will recognize...
  zx::clock dupe_clock, retained_clock, orig_clock = clock::AdjustableCloneOfMonotonic();
  ASSERT_EQ(orig_clock.duplicate(kClockRights, &dupe_clock), ZX_OK);
  ASSERT_EQ(orig_clock.duplicate(kClockRights, &retained_clock), ZX_OK);

  zx::clock::update_args args;
  args.reset().set_rate_adjust(-100);
  ASSERT_EQ(orig_clock.update(args), ZX_OK) << "clock.update with rate_adjust failed";

  // ... and set it on this renderer.
  audio_renderer_->SetReferenceClock(std::move(dupe_clock));
  zx::clock received_clock = GetAndValidateReferenceClock();
  clock::testing::VerifyIsNotSystemMonotonic(received_clock);

  //
  // Now create another clock without transfer rights...
  zx::clock no_transfer_clock = clock::CloneOfMonotonic();
  ASSERT_TRUE(no_transfer_clock.is_valid());
  ASSERT_EQ(no_transfer_clock.replace(kClockRights & ~ZX_RIGHT_TRANSFER, &no_transfer_clock),
            ZX_OK);
  clock::testing::VerifyIsSystemMonotonic(no_transfer_clock);

  // ... and try to set it as our reference clock...
  audio_renderer_->SetReferenceClock(std::move(no_transfer_clock));
  zx::clock received_clock2 = GetAndValidateReferenceClock();

  // ... but this should not result in any change.
  clock::testing::VerifyReadOnlyRights(received_clock2);
  clock::testing::VerifyIsNotSystemMonotonic(received_clock2);
}

// inadequate ZX_RIGHTS -- no DUPLICATE should cause GetReferenceClock to fail.
TEST_F(AudioRendererClockTest, SetRefClock_NoDuplicateShouldDisconnect) {
  zx::clock dupe_clock, orig_clock = clock::CloneOfMonotonic();
  ASSERT_EQ(orig_clock.duplicate(kClockRights & ~ZX_RIGHT_DUPLICATE, &dupe_clock), ZX_OK);

  audio_renderer_->SetReferenceClock(std::move(dupe_clock));
  ExpectDisconnect(audio_renderer_);
}

// inadequate ZX_RIGHTS -- no READ should cause GetReferenceClock to fail.
TEST_F(AudioRendererClockTest, SetRefClock_NoReadShouldDisconnect) {
  zx::clock dupe_clock, orig_clock = clock::CloneOfMonotonic();
  ASSERT_EQ(orig_clock.duplicate(kClockRights & ~ZX_RIGHT_READ, &dupe_clock), ZX_OK);

  audio_renderer_->SetReferenceClock(std::move(dupe_clock));
  ExpectDisconnect(audio_renderer_);
}

// Regardless of the type of clock, calling SetReferenceClock a second time should fail.
TEST_F(AudioRendererClockTest, SetRefClock_CustomThenFlexibleShouldDisconnect) {
  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());

  audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));
  ExpectDisconnect(audio_renderer_);
}

// Regardless of the type of clock, calling SetReferenceClock a second time should fail.
TEST_F(AudioRendererClockTest, SetRefClock_SecondCustomShouldDisconnect) {
  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());

  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());
  ExpectDisconnect(audio_renderer_);
}

// Regardless of the type of clock, calling SetReferenceClock a second time should fail.
TEST_F(AudioRendererClockTest, SetRefClock_SecondFlexibleShouldDisconnect) {
  audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));

  audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));
  ExpectDisconnect(audio_renderer_);
}

// Regardless of the type of clock, calling SetReferenceClock a second time should fail.
TEST_F(AudioRendererClockTest, SetRefClock_FlexibleThenCustomShouldDisconnect) {
  audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));

  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());
  ExpectDisconnect(audio_renderer_);
}

// If client-submitted clock has ZX_RIGHT_WRITE, this should be removed upon GetReferenceClock
TEST_F(AudioRendererClockTest, GetRefClock_RemovesWriteRight) {
  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());

  zx::clock received_clock = GetAndValidateReferenceClock();
  clock::testing::VerifyReadOnlyRights(received_clock);
}

// Setting the reference clock at any time before SetPcmStreamType should pass
TEST_F(AudioRendererClockTest, SetRefClock_AfterAddBuffer) {
  CreateAndAddPayloadBuffer(0);

  audio_renderer_->SetReferenceClock(clock::CloneOfMonotonic());
  auto ref_clock = GetAndValidateReferenceClock();

  clock::testing::VerifyReadOnlyRights(ref_clock);
  clock::testing::VerifyIsSystemMonotonic(ref_clock);
  clock::testing::VerifyAdvances(ref_clock);
  clock::testing::VerifyCannotBeRateAdjusted(ref_clock);
}

// Setting the reference clock at any time afterSetPcmStreamType should fail
TEST_F(AudioRendererClockTest, SetRefClock_AfterSetFormatShouldDisconnect) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->SetReferenceClock(clock::CloneOfMonotonic());
  ExpectDisconnect(audio_renderer_);
}

// Setting the reference clock should fail, if at least one render packet is active
TEST_F(AudioRendererClockTest, SetRefClock_PacketActiveShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  AssertConnectedAndDiscardAllPackets();

  // Even if one packet completes almost immediately, the other will still be outstanding.
  fuchsia::media::StreamPacket packet, packet2;
  packet.payload_buffer_id = packet2.payload_buffer_id = 0;
  packet.payload_offset = packet2.payload_offset = 0;
  packet.payload_size = packet2.payload_size = kDefaultPayloadBufferSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));
  audio_renderer_->SendPacketNoReply(std::move(packet2));

  audio_renderer_->SetReferenceClock(clock::CloneOfMonotonic());
  ExpectDisconnect(audio_renderer_);
}

// Setting the reference clock any time after calling SendPacket should fail, even if packets are no
// longer outstanding
TEST_F(AudioRendererClockTest, SetRefClock_AfterPacketShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  AssertConnectedAndDiscardAllPackets();

  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));
  audio_renderer_->DiscardAllPackets(AddCallback("DiscardAllPackets"));

  // Wait for the Discard completion; now there are no active packets.
  ExpectCallback();

  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());
  ExpectDisconnect(audio_renderer_);
}

// Setting the reference clock at any time after Play should fail
TEST_F(AudioRendererClockTest, SetRefClock_DuringPlayShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                        AddCallback("Play"));
  ExpectCallback();

  // We are now playing, but there are no active packets.
  audio_renderer_->SetReferenceClock(clock::CloneOfMonotonic());
  ExpectDisconnect(audio_renderer_);
}

// Setting the reference clock at any time after Play should fail
TEST_F(AudioRendererClockTest, SetRefClock_AfterPlayShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);
  // We are now playing, but there are no active packets.

  audio_renderer_->Pause(AddCallback("Pause"));
  // Even though we are paused with no packets, SetReferenceClock is still not allowed.
  ExpectCallback();

  audio_renderer_->SetReferenceClock(clock::CloneOfMonotonic());
  ExpectDisconnect(audio_renderer_);
}

}  // namespace media::audio::test
