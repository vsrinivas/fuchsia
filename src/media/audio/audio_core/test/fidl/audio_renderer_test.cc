// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/zx/vmo.h>

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
class AudioRendererTest : public HermeticAudioCoreTest {
 protected:
  void SetUp() override;
  void TearDown() override;
  void SetNegativeExpectations() override;

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

  bool bound_renderer_expected_ = true;
};

//
// AudioRendererTest implementation
//
void AudioRendererTest::SetUp() {
  HermeticAudioCoreTest::SetUp();

  audio_core_->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_renderer_.set_error_handler(ErrorHandler());
}

void AudioRendererTest::TearDown() {
  gain_control_.Unbind();

  EXPECT_EQ(bound_renderer_expected_, audio_renderer_.is_bound());
  audio_renderer_.Unbind();

  HermeticAudioCoreTest::TearDown();
}

void AudioRendererTest::SetNegativeExpectations() {
  HermeticAudioCoreTest::SetNegativeExpectations();
  bound_renderer_expected_ = false;
}

void AudioRendererTest::AssertConnectedAndDiscardAllPackets() {
  audio_renderer_->DiscardAllPackets(CompletionCallback());

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
TEST_F(AudioRendererTest, AddPayloadBufferDuplicateId) {
  CreateAndAddPayloadBuffer(0);
  CreateAndAddPayloadBuffer(0);

  AssertConnectedAndDiscardAllPackets();
}

// It is invalid to add a payload buffer while there are queued packets.
TEST_F(AudioRendererTest, AddPayloadBufferWhileOperationalCausesDisconnect) {
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

  ExpectDisconnect();
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
TEST_F(AudioRendererTest, RemovePayloadBufferInvalidBufferIdCausesDisconnect) {
  audio_renderer_->RemovePayloadBuffer(0);

  ExpectDisconnect();
}

// It is invalid to remove a payload buffer while there are queued packets.
TEST_F(AudioRendererTest, RemovePayloadBufferWhileOperationalCausesDisconnect) {
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

  ExpectDisconnect();
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
  bool callback_received = false;
  audio_renderer_->SendPacket(std::move(packet),
                              [&callback_received] { callback_received = true; });

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP, [](...) {});
  RunLoopUntil([this, &callback_received]() { return error_occurred_ || callback_received; });
  EXPECT_TRUE(callback_received);
}

TEST_F(AudioRendererTest, SendPacketInvokesCallbacksInOrder) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send a packet (we don't care about the actual packet data here).
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  uint32_t callback_count = 0;
  audio_renderer_->SendPacket(fidl::Clone(packet),
                              [&callback_count] { EXPECT_EQ(0u, callback_count++); });
  audio_renderer_->SendPacket(fidl::Clone(packet),
                              [&callback_count] { EXPECT_EQ(1u, callback_count++); });
  audio_renderer_->SendPacket(fidl::Clone(packet),
                              [&callback_count] { EXPECT_EQ(2u, callback_count++); });
  audio_renderer_->SendPacket(fidl::Clone(packet),
                              [&callback_count] { EXPECT_EQ(3u, callback_count++); });

  // Play and expect the callbacks in order.
  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP, [](...) {});

  RunLoopUntil([this, &callback_count]() { return error_occurred_ || (callback_count == 4u); });
  EXPECT_EQ(4u, callback_count);
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

TEST_F(AudioRendererTest, SendPacketNoReplyInvalidPayloadBufferIdCausesDisconnect) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send a packet (we don't care about the actual packet data here).
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 1234;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));

  ExpectDisconnect();
}

// It is invalid to SendPacket before the stream type has been configured
// (SetPcmStreamType).
TEST_F(AudioRendererTest, SendPacketBeforeSetPcmStreamTypeCausesDisconnect) {
  // Add a payload buffer but no stream type.
  CreateAndAddPayloadBuffer(0);

  // SendPacket. This should trigger a disconnect due to a lack of a configured
  // stream type.
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));

  ExpectDisconnect();
}

// SendPacket with a |payload_size| that is invalid.
TEST_F(AudioRendererTest, SendPacketNoReplyInvalidPayloadBufferSizeCausesDisconnect) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send Packet moves connection into the operational state.
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kInvalidPayloadSize;
  audio_renderer_->SendPacketNoReply(std::move(packet));

  ExpectDisconnect();
}

TEST_F(AudioRendererTest, SendPacketNoReplyBufferOutOfBoundsCausesDisconnect) {
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

  ExpectDisconnect();
}

TEST_F(AudioRendererTest, SendPacketNoReplyBufferOverrunCausesDisconnect) {
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

  ExpectDisconnect();
}

// TODO(mpuryear): test EndOfStream();
// Also proper sequence of callbacks/completions

// TODO(mpuryear): test DiscardAllPackets() -> ();
// Also when no packets, when started

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
  audio_renderer_->GetMinLeadTime(CompletionCallback([](int64_t x) {}));
  ExpectCallback();
}

// TODO(mpuryear): test SetPtsUnits(uint32 tick_per_sec_num,uint32 denom);
// Also negative testing: zero values, nullptrs, huge num/small denom

// TODO(mpuryear): test SetPtsContinuityThreshold(float32 threshold_sec);
// Also negative testing: NaN, negative, very large, infinity

// TODO(mpuryear): test SetReferenceClock(handle reference_clock);
// Also negative testing: null handle, bad handle, handle to something else

// TODO(mpuryear): Also: when already in Play, very positive vals, very negative
// vals
TEST_F(AudioRendererTest, Play) {
  // Configure with one buffer and a valid stream type.
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Send a packet (we don't care about the actual packet data here).
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kValidPayloadSize;
  audio_renderer_->SendPacket(std::move(packet), CompletionCallback());

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
  audio_renderer_->SendPacket(std::move(packet), CompletionCallback());

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
  audio_renderer_.events().OnMinLeadTimeChanged = [&min_lead_time](int64_t min_lead_time_nsec) {
    min_lead_time = min_lead_time_nsec;
  };

  audio_renderer_->EnableMinLeadTimeEvents(true);

  // After enabling MinLeadTime events, we expect an initial notification.
  // Because we have not yet set the format, we expect MinLeadTime to be 0.
  RunLoopUntil([this, &min_lead_time]() { return error_occurred_ || (min_lead_time >= 0); });
  EXPECT_EQ(min_lead_time, 0);

  // FYI: after setting format, MinLeadTime should change to be greater than 0
  // IF the target has AudioOutput devices, or remain 0 (no callback) if it has
  // none. Both are valid possibilities, so we don't test that aspect here.
}

// Validate MinLeadTime events, when disabled.
TEST_F(AudioRendererTest, DisableMinLeadTimeEvents) {
  audio_renderer_.events().OnMinLeadTimeChanged =
      CompletionCallback([](int64_t x) { EXPECT_FALSE(true) << kCallbackErr; });

  audio_renderer_->EnableMinLeadTimeEvents(false);

  // We should not receive a OnMinLeadTimeChanged callback (or Disconnect)
  // before receiving this direct GetMinLeadTime callback.
  audio_renderer_->GetMinLeadTime(CompletionCallback([](int64_t x) {}));
  ExpectCallback();
}

//
// Basic validation of GetMinLeadTime() for the asynchronous AudioRenderer.
// Before SetPcmStreamType is called, MinLeadTime should equal zero.
TEST_F(AudioRendererTest, GetMinLeadTime) {
  int64_t min_lead_time = -1;
  audio_renderer_->GetMinLeadTime(
      [&min_lead_time](int64_t min_lead_time_nsec) { min_lead_time = min_lead_time_nsec; });

  // Wait to receive Lead time callback (will loop timeout? EXPECT_FALSE)
  RunLoopUntil([this, &min_lead_time]() { return error_occurred_ || (min_lead_time >= 0); });
  EXPECT_EQ(min_lead_time, 0);
}

// Test creation and interface independence of GainControl.
// In a number of tests below, we run the message loop to give the AudioRenderer
// or GainControl binding a chance to disconnect, if an error occurred.
TEST_F(AudioRendererTest, BindGainControl) {
  // Validate AudioRenderers can create GainControl interfaces.
  audio_renderer_->BindGainControl(gain_control_.NewRequest());
  bool gc_error_occurred = false;
  auto gc_err_handler = [&gc_error_occurred](zx_status_t error) { gc_error_occurred = true; };
  gain_control_.set_error_handler(gc_err_handler);

  fuchsia::media::AudioRendererPtr audio_renderer_2;
  audio_core_->CreateAudioRenderer(audio_renderer_2.NewRequest());
  bool ar2_error_occurred = false;
  auto ar2_err_handler = [&ar2_error_occurred](zx_status_t error) { ar2_error_occurred = true; };
  audio_renderer_2.set_error_handler(ar2_err_handler);

  fuchsia::media::audio::GainControlPtr gain_control_2;
  audio_renderer_2->BindGainControl(gain_control_2.NewRequest());
  bool gc2_error_occurred = false;
  auto gc2_err_handler = [&gc2_error_occurred](zx_status_t error) { gc2_error_occurred = true; };
  gain_control_2.set_error_handler(gc2_err_handler);

  // Validate GainControl2 does NOT persist after audio_renderer_2 is unbound
  audio_renderer_2.Unbind();

  // Validate that audio_renderer_ persists without gain_control_
  gain_control_.Unbind();

  // Give audio_renderer_2 a chance to disconnect gain_control_2
  RunLoopUntil([this, &ar2_error_occurred, &gc_error_occurred, &gc2_error_occurred]() {
    return (error_occurred_ || ar2_error_occurred || gc_error_occurred || gc2_error_occurred);
  });

  // Let audio_renderer_ show it is still alive (and allow other disconnects)
  audio_renderer_->GetMinLeadTime(CompletionCallback([](int64_t x) {}));
  ExpectCallback();

  // Explicitly unbinding audio_renderer_2 should not trigger its disconnect
  // (ar2_error_occurred), but should trigger gain_control_2's disconnect.
  EXPECT_FALSE(ar2_error_occurred);
  EXPECT_TRUE(gc2_error_occurred);
  EXPECT_FALSE(gain_control_2.is_bound());

  // Explicitly unbinding gain_control_ should not trigger its disconnect, nor
  // its parent audio_renderer_'s.
  EXPECT_FALSE(gc_error_occurred);
  EXPECT_TRUE(audio_renderer_.is_bound());
}

// Before setting format, Play should not succeed.
TEST_F(AudioRendererTest, PlayWithoutFormat) {
  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Play(
      fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
      [&ref_time_received, &media_time_received](int64_t ref_time, int64_t media_time) {
        ref_time_received = ref_time;
        media_time_received = media_time;
      });

  // Disconnect callback should be received
  ExpectDisconnect();
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// After setting format but before submitting buffers, Play should not succeed.
TEST_F(AudioRendererTest, PlayWithoutBuffers) {
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
  ExpectDisconnect();
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// Before setting format, PlayNoReply should cause a Disconnect.
TEST_F(AudioRendererTest, PlayNoReplyWithoutFormat) {
  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);

  // Disconnect callback should be received.
  ExpectDisconnect();
}

// Before setting format, Pause should not succeed.
TEST_F(AudioRendererTest, PauseWithoutFormat) {
  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  audio_renderer_->Pause(
      [&ref_time_received, &media_time_received](int64_t ref_time, int64_t media_time) {
        ref_time_received = ref_time;
        media_time_received = media_time;
      });

  // Disconnect callback should be received
  ExpectDisconnect();
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// After setting format but before submitting buffers, Pause should not succeed.
TEST_F(AudioRendererTest, PauseWithoutBuffers) {
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
  ExpectDisconnect();
  EXPECT_EQ(ref_time_received, -1);
  EXPECT_EQ(media_time_received, -1);
}

// Before setting format, PauseNoReply should cause a Disconnect.
TEST_F(AudioRendererTest, PauseNoReplyWithoutFormat) {
  audio_renderer_->PauseNoReply();

  // Disconnect callback should be received.
  ExpectDisconnect();
}

TEST_F(AudioRendererTest, SetUsageAfterSetPcmStreamTypeCausesDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  AssertConnectedAndDiscardAllPackets();

  audio_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION);
  ExpectDisconnect();
}

}  // namespace media::audio::test
