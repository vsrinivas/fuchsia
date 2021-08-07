// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls.h>

#include <cmath>

#include "gtest/gtest.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

// AudioRenderer contains an internal state machine; setting both the buffer and the audio format
// play a central role.
// - Upon construction, a renderer is in the "Initialized" state.
// - To enter "Configured" state, it must receive and successfully execute both SetPcmStreamType and
// AddPayloadBuffer (if only one or the other is called, we remain Initialized).
// - Once Configured, it transitions to "Operating" state, when packets are enqueued (received from
// SendPacket, but not yet played and/or released).
// - Once no enqueued packets remain, it transitions back to Configured state. Packets may be
// cancelled (by DiscardAllPackets), or completed (successfully played); either way their completion
// (if provided) is invoked.

// Additional restrictions on the allowed sequence of API calls:
// SetReferenceClock may only be called once for a given AudioRenderer.
// SetUsage and SetReferenceClock may only be called before SetPcmStreamType.
// SetPcmStreamType, AddPayloadBuffer/RemovePayloadBuffer may only be called when not Operating.
// A renderer must be Configured/Operating before calling SendPacket, Play, Pause.

// Note: the distinction between Configured/Operating is entirely orthogonal to Play/Pause state,
// although Play does cause the timeline to progress, leading to packet completion.

//
// AudioRendererTest
//
// This base class is reused by child classes that provide grouping of specific test areas.
//
// As currently implemented, AudioRenderer's four "NoReply" methods (PlayNoReply, PauseNoReply,
// SendPacketNoReply, DiscardAllPacketsNoReply) each simply redirect to their counterpart with a
// 'nullptr' callback parameter. For this reason, we don't exhaustively test the NoReply variants,
// instead covering them with 1-2 representative test cases each (in addition to those places where
// they are used instead of the "reply" variants for test simplicity).
class AudioRendererTest : public HermeticAudioTest {
 protected:
  // A valid but arbitrary |AudioStreamType|, for tests that don't care about the audio content.
  static constexpr fuchsia::media::AudioStreamType kTestStreamType{
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 2,
      .frames_per_second = 48000,
  };

  // The following are valid/invalid when used with |kTestStreamType|.
  // In bytes: payload buffer 40960 (~ 106 ms); default packet 3840 (10 ms).
  static inline size_t DefaultPayloadBufferSize() { return zx_system_get_page_size() * 10; }
  static constexpr uint64_t kDefaultPacketSize =
      sizeof(float) * kTestStreamType.channels * kTestStreamType.frames_per_second / 100;

  // Convenience packet of 10 ms, starting at the beginning of payload buffer 0.
  static constexpr fuchsia::media::StreamPacket kTestPacket{
      .payload_buffer_id = 0,
      .payload_offset = 0,
      .payload_size = kDefaultPacketSize,
  };

  void SetUp() override {
    HermeticAudioTest::SetUp();

    audio_core_->CreateAudioRenderer(audio_renderer_.NewRequest());
    AddErrorHandler(audio_renderer_, "AudioRenderer");
  }

  void TearDown() override {
    audio_renderer_.Unbind();

    HermeticAudioTest::TearDown();
  }

  // This can be used as a simple round-trip to indicate that all FIDL messages have been read out
  // of the channel, and thus have been handled successfully (i.e. no disconnect was triggered).
  void ExpectConnected() {
    audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime"));

    ExpectCallbacks();
  }

  // Discard in-flight packets and await a renderer response. This checks that the completions for
  // all enqueued packets are received, and that the Discard completion is received only afterward.
  // Thus, this also verifies more generally that the renderer is still connected.
  void ExpectConnectedAndDiscardAllPackets() {
    audio_renderer_->DiscardAllPackets(AddCallback("DiscardAllPackets"));

    ExpectCallbacks();
  }

  // Creates a VMO with |buffer_size| and then passes it to |AudioRenderer::AddPayloadBuffer|
  // with |id|. This is purely a convenience method and doesn't provide access to the buffer VMO.
  void CreateAndAddPayloadBuffer(uint32_t id) {
    zx::vmo payload_buffer;
    constexpr uint32_t kVmoOptionsNone = 0;
    ASSERT_EQ(zx::vmo::create(DefaultPayloadBufferSize(), kVmoOptionsNone, &payload_buffer), ZX_OK);
    audio_renderer_->AddPayloadBuffer(id, std::move(payload_buffer));
  }

  fuchsia::media::AudioRendererPtr audio_renderer_;
};

// AudioRenderer implements the base classes StreamBufferSet and StreamSink.

// Thin wrapper around AudioRendererTest for test case grouping only. This group validates
// AudioRenderer's implementation of StreamBufferSet (AddPayloadBuffer, RemovePayloadBuffer)
class AudioRendererBufferTest : public AudioRendererTest {};

// Sanity test adding a payload buffer. Just verify we don't get a disconnect.
TEST_F(AudioRendererBufferTest, AddPayloadBuffer) {
  CreateAndAddPayloadBuffer(0);
  CreateAndAddPayloadBuffer(1);
  CreateAndAddPayloadBuffer(2);

  ExpectConnectedAndDiscardAllPackets();
}

// TODO(fxbug.dev/77815): This is out-of-spec, but clients rely on AddPayloadBuffer(existing_id) to
// replace or update an already-added buffer. Update this test to fail, once all clients are fixed.
TEST_F(AudioRendererBufferTest, AddPayloadBufferDuplicateId) {
  CreateAndAddPayloadBuffer(0);
  CreateAndAddPayloadBuffer(0);

  ExpectConnectedAndDiscardAllPackets();
}

// AddPayloadBuffer is callable after packets are completed/discarded, regardless of play/pause
TEST_F(AudioRendererBufferTest, AddPayloadBufferWhileNotOperating) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket1"));
  ExpectConnectedAndDiscardAllPackets();
  CreateAndAddPayloadBuffer(1);

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, AddCallback("Play"));
  CreateAndAddPayloadBuffer(2);

  ExpectCallbacks();
  CreateAndAddPayloadBuffer(3);

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket2"));
  ExpectCallbacks();
  CreateAndAddPayloadBuffer(4);

  audio_renderer_->Pause(AddCallback("Pause"));
  CreateAndAddPayloadBuffer(5);

  ExpectCallbacks();
  CreateAndAddPayloadBuffer(6);

  ExpectConnectedAndDiscardAllPackets();
}

// It is invalid to add a payload buffer while there are queued packets.
// Attempt to add new payload buffer while the packet is in flight. This should fail.
TEST_F(AudioRendererBufferTest, AddPayloadBufferWhileOperatingShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  audio_renderer_->SendPacketNoReply(kTestPacket);

  CreateAndAddPayloadBuffer(1);

  ExpectDisconnect(audio_renderer_);
}

// Test removing payload buffers.
TEST_F(AudioRendererBufferTest, RemovePayloadBuffer) {
  CreateAndAddPayloadBuffer(0);
  CreateAndAddPayloadBuffer(1);
  CreateAndAddPayloadBuffer(2);
  CreateAndAddPayloadBuffer(3);
  audio_renderer_->RemovePayloadBuffer(2);
  audio_renderer_->RemovePayloadBuffer(3);
  audio_renderer_->RemovePayloadBuffer(0);
  audio_renderer_->RemovePayloadBuffer(1);

  ExpectConnectedAndDiscardAllPackets();
}

// RemovePayloadBuffer is callable at ANY time if no packets are active
TEST_F(AudioRendererBufferTest, RemovePayloadBufferWhileNotOperating) {
  CreateAndAddPayloadBuffer(0);
  CreateAndAddPayloadBuffer(1);
  CreateAndAddPayloadBuffer(2);
  CreateAndAddPayloadBuffer(3);
  CreateAndAddPayloadBuffer(4);
  CreateAndAddPayloadBuffer(5);
  CreateAndAddPayloadBuffer(6);
  CreateAndAddPayloadBuffer(7);
  audio_renderer_->RemovePayloadBuffer(1);  // Don't remove buffer 0 yet: we use it in SendPacket

  audio_renderer_->SetPcmStreamType(kTestStreamType);
  audio_renderer_->RemovePayloadBuffer(2);

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket1"));
  ExpectConnectedAndDiscardAllPackets();  // cancel the packet and wait until it returns
  audio_renderer_->RemovePayloadBuffer(3);

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, AddCallback("Play"));
  audio_renderer_->RemovePayloadBuffer(4);

  ExpectCallbacks();  // wait until Play completes
  audio_renderer_->RemovePayloadBuffer(5);

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket2"));
  ExpectCallbacks();  // wait until the packet completes normally
  audio_renderer_->RemovePayloadBuffer(6);

  audio_renderer_->Pause(AddCallback("Pause"));
  audio_renderer_->RemovePayloadBuffer(7);

  ExpectCallbacks();  // wait until Pause completes
  audio_renderer_->RemovePayloadBuffer(0);

  ExpectConnected();
}

// It is invalid to remove a payload buffer while there are queued packets.
TEST_F(AudioRendererBufferTest, RemovePayloadBufferWhileOperatingShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  ExpectConnected();  // ensure that if/when we disconnect, it is not because of the above

  audio_renderer_->SendPacketNoReply(kTestPacket);

  audio_renderer_->RemovePayloadBuffer(0);

  ExpectDisconnect(audio_renderer_);
}

// Test RemovePayloadBuffer with an invalid ID (no corresponding AddPayloadBuffer).
TEST_F(AudioRendererBufferTest, RemovePayloadBufferInvalidBufferIdShouldDisconnect) {
  audio_renderer_->RemovePayloadBuffer(0);

  ExpectDisconnect(audio_renderer_);
}

//
// StreamSink validation
//

// Thin wrapper around AudioRendererTest for test case grouping only. This group validates
// AudioRenderer's implementation of StreamSink (SendPacket, DiscardAllPackets, EndOfStream).
class AudioRendererPacketTest : public AudioRendererTest {
 protected:
  // SetPcmStreamType and AddPayloadBuffer are callable in either order, as long as both are called
  // before Play. Thus, in these tests you see a mixture.
  void SendPacketCancellation(bool reply) {
    CreateAndAddPayloadBuffer(0);
    audio_renderer_->SetPcmStreamType(kTestStreamType);

    // Send a packet (we don't care about the actual packet data here).
    if (reply) {
      audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));
    } else {
      audio_renderer_->SendPacketNoReply(kTestPacket);
    }

    ExpectConnectedAndDiscardAllPackets();
  }
};

TEST_F(AudioRendererPacketTest, SendPacketCompletion) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  CreateAndAddPayloadBuffer(0);

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                        [](int64_t, int64_t) {});
  ExpectCallbacks();
}

TEST_F(AudioRendererPacketTest, SendPacketInvokesCallbacksInOrder) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Play will complete and then each packet successively, so create this callback first.
  auto play_callback = AddCallback("Play");

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket1"));
  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket2"));
  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket3"));
  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket4"));

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP, play_callback);
  ExpectCallbacks();
}

TEST_F(AudioRendererPacketTest, SendPacketCancellation) { SendPacketCancellation(true); }
// This is the sole test case to expressly target SendPacketNoReply.
TEST_F(AudioRendererPacketTest, SendPacketNoReplyCancellation) { SendPacketCancellation(false); }

TEST_F(AudioRendererPacketTest, SendPacketTooManyShouldDisconnect) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  CreateAndAddPayloadBuffer(0);

  // The exact limit is a function of the size of some internal data structures. We verify this
  // limit is somewhere between 500 and 600 packets.
  for (int i = 0; i < 500; ++i) {
    audio_renderer_->SendPacket(kTestPacket, []() {});
  }
  ExpectConnectedAndDiscardAllPackets();

  for (int i = 0; i < 600; ++i) {
    audio_renderer_->SendPacket(kTestPacket, []() {});
  }
  ExpectDisconnect(audio_renderer_);
}

// SendPacket cannot be called before the stream type has been configured (SetPcmStreamType).
TEST_F(AudioRendererPacketTest, SendPacketWithoutFormatShouldDisconnect) {
  // Add a payload buffer but no stream type.
  CreateAndAddPayloadBuffer(0);

  // SendPacket should trigger a disconnect due to a lack of a configured stream type.
  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));

  ExpectDisconnect(audio_renderer_);
}

// SendPacket cannot be called before the payload buffer has been added.
TEST_F(AudioRendererPacketTest, SendPacketWithoutBufferShouldDisconnect) {
  // Add a stream type but no payload buffer.
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // SendPacket should trigger a disconnect due to a lack of a configured stream type.
  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));

  ExpectDisconnect(audio_renderer_);
}

// SendPacket with an unknown |payload_buffer_id|
TEST_F(AudioRendererPacketTest, SendPacketInvalidPayloadBufferIdShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // We never added a payload buffer with this ID, so this should cause a disconnect
  auto packet = kTestPacket;
  packet.payload_buffer_id = 1234;
  audio_renderer_->SendPacket(std::move(packet), []() {});

  ExpectDisconnect(audio_renderer_);
}

// SendPacket with a |payload_size| that is invalid
TEST_F(AudioRendererPacketTest, SendPacketInvalidPayloadBufferSizeShouldDisconnect) {
  // kTestStreamType frames are 8 bytes (float32 x Stereo).
  // As an invalid packet size, we specify a value (9) that is NOT a perfect multiple of 8.
  constexpr uint64_t kInvalidPayloadSize = sizeof(float) * kTestStreamType.channels + 1;

  audio_renderer_->SetPcmStreamType(kTestStreamType);
  CreateAndAddPayloadBuffer(0);

  auto packet = kTestPacket;
  packet.payload_size = kInvalidPayloadSize;
  audio_renderer_->SendPacket(std::move(packet), []() {});

  ExpectDisconnect(audio_renderer_);
}

// |payload_offset| starts beyond the end of the payload buffer.
TEST_F(AudioRendererPacketTest, SendPacketBufferOutOfBoundsShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  auto packet = kTestPacket;
  packet.payload_offset = DefaultPayloadBufferSize();
  audio_renderer_->SendPacket(std::move(packet), []() {});

  ExpectDisconnect(audio_renderer_);
}

// |payload_offset| + |payload_size| extends beyond the end of the payload buffer.
TEST_F(AudioRendererPacketTest, SendPacketBufferOverrunShouldDisconnect) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  CreateAndAddPayloadBuffer(0);

  auto packet = kTestPacket;
  packet.payload_size = kDefaultPacketSize * 2;
  packet.payload_offset = DefaultPayloadBufferSize() - kDefaultPacketSize;
  audio_renderer_->SendPacket(std::move(packet), []() {});

  ExpectDisconnect(audio_renderer_);
}

// DiscardAllPackets cancels any outstanding (uncompleted) packets. Whether they complete normally
// or are cancelled before playing out, ALL packet callbacks should be invoked. These should be
// received in the original SendPacket order, followed finally by the DiscardAllPackets callback.
TEST_F(AudioRendererPacketTest, DiscardAllPacketsReturnsAfterAllPackets) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Even if the first packet completes almost immediately, others will still be outstanding.
  auto packet = kTestPacket;
  packet.payload_size = DefaultPayloadBufferSize();

  audio_renderer_->SendPacket(fidl::Clone(packet), AddCallback("SendPacket1"));
  audio_renderer_->SendPacket(fidl::Clone(packet), AddCallback("SendPacket2"));
  audio_renderer_->SendPacket(fidl::Clone(packet), AddCallback("SendPacket3"));
  audio_renderer_->SendPacket(std::move(packet), AddCallback("SendPacket4"));

  // We don't actually care where Play callback occurs in this sequence so we don't AddCallback it.
  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);

  audio_renderer_->DiscardAllPackets(AddCallback("DiscardAllPackets"));

  // Our sequence of AddCallback calls reflects the expected ordering of callback invocation.
  // ExpectCallbacks enforces this ordering, and no unexpected callbacks, and no disconnects.
  ExpectCallbacks();
}

// This is the sole test case to expressly target DiscardAllPacketsNoReply.
// Packets are cancelled; completion callbacks should be invoked in-order.
TEST_F(AudioRendererPacketTest, DiscardAllPacketsNoReply) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  CreateAndAddPayloadBuffer(0);

  auto packet = kTestPacket;
  packet.payload_size = DefaultPayloadBufferSize();
  audio_renderer_->SendPacket(fidl::Clone(packet), AddCallback("SendPacket1"));
  audio_renderer_->SendPacket(fidl::Clone(packet), AddCallback("SendPacket2"));
  audio_renderer_->SendPacket(fidl::Clone(packet), AddCallback("SendPacket3"));
  audio_renderer_->SendPacket(std::move(packet), AddCallback("SendPacket4"));

  audio_renderer_->DiscardAllPacketsNoReply();

  ExpectCallbacks();
}

// Ensure that calling Discard before Play/Pause doesn't prevent the timeline from progressing.
TEST_F(AudioRendererPacketTest, DiscardAllPacketsBeforePlayDoesntComputeTimeline) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->DiscardAllPackets(AddCallback("DiscardAllPackets"));

  int64_t play_ref_time = -1, play_media_time = -1;
  int64_t pause_ref_time = -1, pause_media_time = -1;

  audio_renderer_->Play(
      fuchsia::media::NO_TIMESTAMP, 0,
      AddCallback("Play", [&play_ref_time, &play_media_time](auto ref_time, auto media_time) {
        play_ref_time = ref_time;
        play_media_time = media_time;
      }));

  ExpectCallbacks();
  EXPECT_EQ(play_media_time, 0);

  // If we call Play(NO_TIMESTAMP) then Pause immediately, it is possible for pause_ref_time <
  // play_ref_time. Even for ref_time NO_TIMESTAMP, audio_core still applies a small padding to the
  // effective Play ref_time, to guarantee that we can start exactly when we said we would.
  //
  // If pause_ref_time IS less than play_ref_time, the equivalent pause_media_time would be
  // negative. This is not necessarily incorrect behavior but would certainly confuse a caller.
  // Let's avoid the problem by adding this slight delay:
  do {
    zx_nanosleep(play_ref_time);
  } while (zx_clock_get_monotonic() < play_ref_time);

  audio_renderer_->Pause(
      AddCallback("Pause", [&pause_ref_time, &pause_media_time](auto ref_time, auto media_time) {
        pause_ref_time = ref_time;
        pause_media_time = media_time;
      }));

  ExpectCallbacks();

  // Renderer calculates Pause's media_time from its timeline function, which should be running.
  EXPECT_GT(pause_ref_time, play_ref_time);
  EXPECT_GT(pause_media_time, play_media_time);
}

// EndOfStream can be called at any time, regardless of the renderer's state.
TEST_F(AudioRendererPacketTest, EndOfStreamIsAlwaysCallable) {
  audio_renderer_->EndOfStream();

  CreateAndAddPayloadBuffer(0);
  audio_renderer_->EndOfStream();

  audio_renderer_->SetPcmStreamType(kTestStreamType);
  audio_renderer_->EndOfStream();

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, AddCallback("Play"));
  audio_renderer_->EndOfStream();

  ExpectCallbacks();
  audio_renderer_->EndOfStream();

  // Send a packet and allow it to drain out.
  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));
  audio_renderer_->EndOfStream();

  ExpectCallbacks();
  audio_renderer_->EndOfStream();

  audio_renderer_->Pause(AddCallback("Pause"));
  audio_renderer_->EndOfStream();

  ExpectCallbacks();
  audio_renderer_->EndOfStream();

  ExpectConnected();  // Demonstrate we haven't disconnected
}

// Thin wrapper around AudioRendererTest for test case grouping only. This group tests
// AudioRenderer's implementation of SetReferenceClock and GetReferenceClock.
class AudioRendererClockTest : public AudioRendererTest {
 protected:
  // The clock received from GetRefClock is read-only, but the original can still be adjusted.
  static constexpr auto kClockRights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ;

  zx::clock GetAndValidateReferenceClock() {
    zx::clock clock;

    audio_renderer_->GetReferenceClock(
        AddCallback("GetReferenceClock",
                    [&clock](zx::clock received_clock) { clock = std::move(received_clock); }));

    ExpectCallbacks();

    return clock;
  }
};

// If client-submitted clock has ZX_RIGHT_WRITE, this should be removed by GetReferenceClock
TEST_F(AudioRendererClockTest, GetRefClockRemovesWriteRight) {
  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());

  zx::clock received_clock = GetAndValidateReferenceClock();
  clock::testing::VerifyReadOnlyRights(received_clock);
}

// Accept the default clock that is returned if we set no clock
TEST_F(AudioRendererClockTest, SetRefClockDefault) {
  zx::clock ref_clock = GetAndValidateReferenceClock();

  clock::testing::VerifyReadOnlyRights(ref_clock);
  clock::testing::VerifyIsSystemMonotonic(ref_clock);

  clock::testing::VerifyAdvances(ref_clock);
  clock::testing::VerifyCannotBeRateAdjusted(ref_clock);
}

// Set a null clock; this represents selecting the AudioCore-generated clock.
TEST_F(AudioRendererClockTest, SetRefClockFlexible) {
  audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));
  zx::clock provided_clock = GetAndValidateReferenceClock();

  clock::testing::VerifyReadOnlyRights(provided_clock);
  clock::testing::VerifyIsSystemMonotonic(provided_clock);

  clock::testing::VerifyAdvances(provided_clock);
  clock::testing::VerifyCannotBeRateAdjusted(provided_clock);
}

// Set a recognizable custom reference clock and validate that it is what we receive from
// GetReferenceClock. The received clock should be read-only; the original is still adjustable.
TEST_F(AudioRendererClockTest, SetRefClockCustom) {
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

  clock::testing::VerifyCanBeRateAdjusted(orig_clock);
  clock::testing::VerifyAdvances(orig_clock);
}

// inadequate ZX_RIGHTS (no DUPLICATE) should cause GetReferenceClock to fail.
TEST_F(AudioRendererClockTest, SetRefClockWithoutDuplicateShouldDisconnect) {
  zx::clock dupe_clock, orig_clock = clock::CloneOfMonotonic();
  ASSERT_EQ(orig_clock.duplicate(kClockRights & ~ZX_RIGHT_DUPLICATE, &dupe_clock), ZX_OK);

  audio_renderer_->SetReferenceClock(std::move(dupe_clock));
  ExpectDisconnect(audio_renderer_);
}

// inadequate ZX_RIGHTS (no READ) should cause GetReferenceClock to fail.
TEST_F(AudioRendererClockTest, SetRefClockWithoutReadShouldDisconnect) {
  zx::clock dupe_clock, orig_clock = clock::CloneOfMonotonic();
  ASSERT_EQ(orig_clock.duplicate(kClockRights & ~ZX_RIGHT_READ, &dupe_clock), ZX_OK);

  audio_renderer_->SetReferenceClock(std::move(dupe_clock));
  ExpectDisconnect(audio_renderer_);
}

// Regardless of the type of clock, calling SetReferenceClock a second time should fail.
// Set a custom clock, then try to select the audio_core supplied 'flexible' clock.
TEST_F(AudioRendererClockTest, SetRefClockCustomThenFlexibleShouldDisconnect) {
  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());

  audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));
  ExpectDisconnect(audio_renderer_);
}

// Regardless of the type of clock, calling SetReferenceClock a second time should fail.
// Select the audio_core supplied 'flexible' clock, then try to set a custom clock.
TEST_F(AudioRendererClockTest, SetRefClockFlexibleThenCustomShouldDisconnect) {
  audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));

  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());
  ExpectDisconnect(audio_renderer_);
}

// Regardless of the type of clock, calling SetReferenceClock a second time should fail.
// Set a custom clock, then try to set a different custom clock.
TEST_F(AudioRendererClockTest, SetRefClockSecondCustomShouldDisconnect) {
  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());

  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());
  ExpectDisconnect(audio_renderer_);
}

// Regardless of the type of clock, calling SetReferenceClock a second time should fail.
// Select the audio_core supplied 'flexible' clock, then make the same call a second time.
TEST_F(AudioRendererClockTest, SetRefClockSecondFlexibleShouldDisconnect) {
  audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));

  audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));
  ExpectDisconnect(audio_renderer_);
}

// Setting the reference clock at any time before SetPcmStreamType should pass
TEST_F(AudioRendererClockTest, SetRefClockAfterAddBuffer) {
  CreateAndAddPayloadBuffer(0);

  audio_renderer_->SetReferenceClock(clock::CloneOfMonotonic());
  auto ref_clock = GetAndValidateReferenceClock();

  clock::testing::VerifyReadOnlyRights(ref_clock);
  clock::testing::VerifyIsSystemMonotonic(ref_clock);
  clock::testing::VerifyAdvances(ref_clock);
  clock::testing::VerifyCannotBeRateAdjusted(ref_clock);
}

// Setting the reference clock at any time afterSetPcmStreamType should fail
TEST_F(AudioRendererClockTest, SetRefClockAfterSetFormatShouldDisconnect) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->SetReferenceClock(clock::CloneOfMonotonic());
  ExpectDisconnect(audio_renderer_);
}

// Once the format is set, setting a ref clock should fail even if post-Pause with no packets.
TEST_F(AudioRendererClockTest, SetRefClockAfterPacketShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->SendPacketNoReply(kTestPacket);

  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);
  audio_renderer_->Pause(AddCallback("Pause"));
  ExpectCallbacks();

  audio_renderer_->DiscardAllPackets(AddCallback("DiscardAllPackets"));
  ExpectCallbacks();

  audio_renderer_->SetReferenceClock(clock::AdjustableCloneOfMonotonic());
  ExpectDisconnect(audio_renderer_);
}

// Thin wrapper around AudioRendererTest for grouping only. This tests EnableMinLeadTimeEvents,
// GetMinLeadTime and OnMinLeadTimeChanged, as well as SetPtsUnits and SetPtsContinuityThreshold.
class AudioRendererPtsLeadTimeTest : public AudioRendererTest {};

// Validate MinLeadTime events, when enabled. After enabling MinLeadTime events, we expect an
// initial notification. Because we have not yet set the format, we expect MinLeadTime to be 0.
TEST_F(AudioRendererPtsLeadTimeTest, EnableMinLeadTimeEventsBeforeFormat) {
  int64_t min_lead_time = -1;
  audio_renderer_.events().OnMinLeadTimeChanged = AddCallback(
      "OnMinLeadTimeChanged",
      [&min_lead_time](int64_t min_lead_time_nsec) { min_lead_time = min_lead_time_nsec; });

  audio_renderer_->EnableMinLeadTimeEvents(true);

  ExpectCallbacks();
  EXPECT_EQ(min_lead_time, 0);
}

// After setting format, MinLeadTime changes to reflect the delay properties of the output device,
// once it has been initialized to a certain audio format.
//
// If there is no valid output device, lead time remains 0 even after SetPcmStreamType is called
// (and no additional OnMinLeadTimeChanged event is generated). We don't test that behavior here.
//
// In this case, post-SetPcmStreamType lead time > 0 (RendererShim includes an AudioOutput).
TEST_F(AudioRendererPtsLeadTimeTest, EnableMinLeadTimeEventsAfterFormat) {
  audio_renderer_.events().OnMinLeadTimeChanged = AddCallback("OnMinLeadTimeChanged1");
  audio_renderer_->EnableMinLeadTimeEvents(true);
  ExpectCallbacks();

  int64_t lead_time = 0;
  audio_renderer_.events().OnMinLeadTimeChanged =
      AddCallback("OnMinLeadTimeChanged2",
                  [&lead_time](int64_t lead_time_nsec) { lead_time = lead_time_nsec; });
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  ExpectCallbacks();
  EXPECT_GT(lead_time, 0);
}

// Validate no MinLeadTime events when disabled (nor should we Disconnect).
TEST_F(AudioRendererPtsLeadTimeTest, DisableMinLeadTimeEvents) {
  audio_renderer_.events().OnMinLeadTimeChanged = AddUnexpectedCallback("OnMinLeadTimeChanged");

  audio_renderer_->EnableMinLeadTimeEvents(false);
  ExpectConnected();

  audio_renderer_->SetPcmStreamType(kTestStreamType);
  ExpectConnected();
}

// Before SetPcmStreamType is called, MinLeadTime should equal zero.
TEST_F(AudioRendererPtsLeadTimeTest, GetMinLeadTimeBeforeFormat) {
  int64_t min_lead_time = -1;
  audio_renderer_->GetMinLeadTime(AddCallback(
      "GetMinLeadTime",
      [&min_lead_time](int64_t min_lead_time_nsec) { min_lead_time = min_lead_time_nsec; }));

  ExpectCallbacks();
  EXPECT_EQ(min_lead_time, 0);
}

// EnableMinLeadTimeEvents can be called at any time, regardless of the renderer's state.
TEST_F(AudioRendererPtsLeadTimeTest, EnableMinLeadTimeEventsCanAlwaysBeCalled) {
  audio_renderer_->EnableMinLeadTimeEvents(true);

  audio_renderer_->SetPcmStreamType(kTestStreamType);
  audio_renderer_->EnableMinLeadTimeEvents(false);

  CreateAndAddPayloadBuffer(0);
  audio_renderer_->EnableMinLeadTimeEvents(true);

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, AddCallback("Play"));
  audio_renderer_->EnableMinLeadTimeEvents(false);

  ExpectCallbacks();
  audio_renderer_->EnableMinLeadTimeEvents(true);

  // Send a packet and allow it to drain out.
  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));
  audio_renderer_->EnableMinLeadTimeEvents(false);

  ExpectCallbacks();
  audio_renderer_->EnableMinLeadTimeEvents(true);

  audio_renderer_->Pause(AddCallback("Pause"));
  audio_renderer_->EnableMinLeadTimeEvents(false);

  ExpectCallbacks();
  audio_renderer_->EnableMinLeadTimeEvents(true);

  ExpectConnected();  // Demonstrate we haven't disconnected
}

// Verify that GetMinLeadTime can be called at any time, regardless of the renderer's state.
TEST_F(AudioRendererPtsLeadTimeTest, GetMinLeadTimeCanAlwaysBeCalled) {
  audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime1"));
  ExpectCallbacks();

  CreateAndAddPayloadBuffer(0);
  audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime2"));
  ExpectCallbacks();

  audio_renderer_->SetPcmStreamType(kTestStreamType);
  audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime3"));
  ExpectCallbacks();

  // We use PlayNoReply and PauseNoReply here because there is no required callback ordering between
  // Play/Pause completion and the GetMinLeadTime callback.
  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, 0);
  audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime4"));
  ExpectCallbacks();

  // Send a packet and allow it to drain out.
  audio_renderer_->SendPacketNoReply(kTestPacket);
  audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime5"));
  ExpectCallbacks();

  audio_renderer_->PauseNoReply();
  audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime6"));
  ExpectCallbacks();

  ExpectConnectedAndDiscardAllPackets();  // Demonstrate we haven't disconnected
  audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime7"));
  ExpectCallbacks();
}

// SetPtsUnits accepts uint numerator and denominator that must be within certain range
TEST_F(AudioRendererPtsLeadTimeTest, SetPtsUnitsZeroNumeratorShouldDisconnect) {
  audio_renderer_->SetPtsUnits(0, 1);
  ExpectDisconnect(audio_renderer_);
}

TEST_F(AudioRendererPtsLeadTimeTest, SetPtsUnitsTooHighShouldDisconnect) {
  audio_renderer_->SetPtsUnits(1'000'000'001, 1);
  ExpectDisconnect(audio_renderer_);
}

TEST_F(AudioRendererPtsLeadTimeTest, SetPtsUnitsZeroDenominatorShouldDisconnect) {
  audio_renderer_->SetPtsUnits(1000, 0);
  ExpectDisconnect(audio_renderer_);
}

TEST_F(AudioRendererPtsLeadTimeTest, SetPtsUnitsTooLowShouldDisconnect) {
  audio_renderer_->SetPtsUnits(1, 61);
  ExpectDisconnect(audio_renderer_);
}

// SetPtsUnits can be called at any time, except when active packets are outstanding
TEST_F(AudioRendererPtsLeadTimeTest, SetPtsUnitsWhileNotOperating) {
  audio_renderer_->SetPtsUnits(kTestStreamType.frames_per_second, 1);

  audio_renderer_->SetPcmStreamType(kTestStreamType);
  audio_renderer_->SetPtsUnits(kTestStreamType.frames_per_second, 2);

  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPtsUnits(kTestStreamType.frames_per_second, 3);

  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, 0);
  audio_renderer_->SetPtsUnits(kTestStreamType.frames_per_second, 1);

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));
  ExpectCallbacks();  // Allow the sent packet to drain out.
  audio_renderer_->SetPtsUnits(kTestStreamType.frames_per_second * 2, 1);

  audio_renderer_->PauseNoReply();
  audio_renderer_->SetPtsUnits(kTestStreamType.frames_per_second * 3, 1);

  ExpectConnected();  // Demonstrate we haven't disconnected
}

TEST_F(AudioRendererPtsLeadTimeTest, SetPtsUnitsWhileOperatingShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->SendPacketNoReply(kTestPacket);
  audio_renderer_->SetPtsUnits(kTestStreamType.frames_per_second, 1);

  ExpectDisconnect(audio_renderer_);
}

// SetPtsContinuityThreshold is callable at any time, except when active packets are outstanding
TEST_F(AudioRendererPtsLeadTimeTest, SetPtsContThresholdWhileNotOperating) {
  audio_renderer_->SetPtsContinuityThreshold(0.0f);

  audio_renderer_->SetPcmStreamType(kTestStreamType);
  audio_renderer_->SetPtsContinuityThreshold(0.01f);

  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPtsContinuityThreshold(0.02f);

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, AddCallback("Play"));
  audio_renderer_->SetPtsContinuityThreshold(0.03f);

  ExpectCallbacks();
  audio_renderer_->SetPtsContinuityThreshold(0.04f);

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));
  ExpectCallbacks();  // Send a packet and allow it to drain out.
  audio_renderer_->SetPtsContinuityThreshold(0.05f);

  audio_renderer_->Pause(AddCallback("Pause"));
  audio_renderer_->SetPtsContinuityThreshold(0.06f);

  ExpectCallbacks();
  audio_renderer_->SetPtsContinuityThreshold(0.07f);

  ExpectConnected();  // Demonstrate we haven't disconnected
}

// If active packets are outstanding, calling SetPtsContinuityThreshold will cause a disconnect
TEST_F(AudioRendererPtsLeadTimeTest, SetPtsContThresholdWhileOperatingCausesDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->SendPacketNoReply(kTestPacket);
  audio_renderer_->SetPtsContinuityThreshold(0.01f);

  ExpectDisconnect(audio_renderer_);
}

// SetPtsContinuityThreshold parameter must be non-negative
TEST_F(AudioRendererPtsLeadTimeTest, SetPtsContThresholdNegativeValueCausesDisconnect) {
  audio_renderer_->SetPtsContinuityThreshold(-0.01f);
  ExpectDisconnect(audio_renderer_);
}

// SetPtsContinuityThreshold parameter must be a normal number
TEST_F(AudioRendererPtsLeadTimeTest, SetPtsContThresholdNanCausesDisconnect) {
  audio_renderer_->SetPtsContinuityThreshold(NAN);
  ExpectDisconnect(audio_renderer_);
}

// SetPtsContinuityThreshold parameter must be a finite number
TEST_F(AudioRendererPtsLeadTimeTest, SetPtsContThresholdInfinityCausesDisconnect) {
  audio_renderer_->SetPtsContinuityThreshold(INFINITY);
  ExpectDisconnect(audio_renderer_);
}

// SetPtsContinuityThreshold parameter must be a number within the finite range
TEST_F(AudioRendererPtsLeadTimeTest, SetPtsContThresholdHugeValCausesDisconnect) {
  audio_renderer_->SetPtsContinuityThreshold(HUGE_VALF);
  ExpectDisconnect(audio_renderer_);
}

// SetPtsContinuityThreshold parameter must be a normal (not sub-normal) number
TEST_F(AudioRendererPtsLeadTimeTest, SetPtsContThresholdSubNormalValCausesDisconnect) {
  audio_renderer_->SetPtsContinuityThreshold(FLT_MIN / 2);
  ExpectDisconnect(audio_renderer_);
}

// Thin wrapper around AudioRendererTest for test case grouping only.
// This group validates AudioRenderer's implementation of SetUsage and SetPcmStreamType.
class AudioRendererFormatUsageTest : public AudioRendererTest {};

// A renderer stream's usage can be changed any time before the format is set.
TEST_F(AudioRendererFormatUsageTest, SetUsageBeforeFormat) {
  audio_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION);

  audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));
  audio_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);

  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::INTERRUPTION);

  audio_renderer_->GetReferenceClock(AddCallback("GetReferenceClock"));
  audio_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::BACKGROUND);
  ExpectCallbacks();

  audio_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::MEDIA);
  ExpectConnected();  // Demonstrate we haven't disconnected
}

// Once the format has been set, SetUsage may no longer be called any time thereafter.
TEST_F(AudioRendererFormatUsageTest, SetUsageAfterFormatShouldDisconnect) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  audio_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION);

  ExpectDisconnect(audio_renderer_);
}

// ... this restriction is not lifted even after all packets have been returned.
TEST_F(AudioRendererFormatUsageTest, SetUsageAfterOperatingShouldDisconnect) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, 0);

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));
  ExpectCallbacks();  // Send a packet and allow it to drain out.

  audio_renderer_->Pause(AddCallback("Pause"));
  ExpectCallbacks();

  audio_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::BACKGROUND);

  ExpectDisconnect(audio_renderer_);
}

// Before renderers are Operating, SetPcmStreamType should succeed. Test twice because of a previous
// bug, where the first call succeeded but the second (pre-Play) caused a disconnect.
TEST_F(AudioRendererFormatUsageTest, SetPcmStreamType) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->SetPcmStreamType({
      .sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8,
      .channels = 1,
      .frames_per_second = 44100,
  });

  ExpectConnected();  // Allow for a Disconnect; expect a valid GetMinLeadTime callback instead
}

// Setting PCM format within supportable ranges should succeed, if no active packets.
// Test both post-cancellation and post-completion scenarios. This is the only test case to
TEST_F(AudioRendererFormatUsageTest, SetPcmStreamTypeAfterOperating) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket to discard"));
  audio_renderer_->DiscardAllPacketsNoReply();
  ExpectCallbacks();  // Wait for the packet to cancel/return

  audio_renderer_->SetPcmStreamType({
      .sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8,
      .channels = 1,
      .frames_per_second = 44100,
  });

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket to play"));
  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);
  ExpectCallbacks();  // Wait for the packet to complete normally

  audio_renderer_->SetPcmStreamType({
      .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
      .channels = 2,
      .frames_per_second = 44100,
  });

  ExpectConnected();
}

TEST_F(AudioRendererFormatUsageTest, SetPcmStreamTypeWhileOperatingShouldDisconnect) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SendPacketNoReply(kTestPacket);

  audio_renderer_->SetPcmStreamType({
      .sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8,
      .channels = 1,
      .frames_per_second = 44100,
  });

  ExpectDisconnect(audio_renderer_);
}

// Thin wrapper around AudioRendererTest for test case grouping only.
// This group validates AudioRenderer's implementation of Play and Pause.
class AudioRendererTransportTest : public AudioRendererTest {};

TEST_F(AudioRendererTransportTest, Play) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  auto packet = kTestPacket;
  packet.pts = ZX_MSEC(100);

  // We expect to receive |Play| callback _before_ |SendPacket| callback, so we add it first.
  auto play_callback = AddCallback("Play");
  audio_renderer_->SendPacket(std::move(packet), AddCallback("SendPacket"));
  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, play_callback);

  ExpectCallbacks();
}

// This is the sole test case to expressly target PlayNoReply, although it is used elsewhere.
TEST_F(AudioRendererTransportTest, PlayNoReply) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));

  audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);

  ExpectCallbacks();
}

// Without a format, Play should not succeed.
TEST_F(AudioRendererTransportTest, PlayWithoutFormatShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                        AddUnexpectedCallback("Play"));
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  ExpectDisconnect(audio_renderer_);
}

// Without a payload buffer, Play should not succeed.
TEST_F(AudioRendererTransportTest, PlayWithoutBufferShouldDisconnect) {
  audio_renderer_->SetPcmStreamType({
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 1,
      .frames_per_second = 32000,
  });

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                        AddUnexpectedCallback("Play"));
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  ExpectDisconnect(audio_renderer_);
}

TEST_F(AudioRendererTransportTest, PlayWithLargeReferenceTimeShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));

  constexpr int64_t kLargeTimestamp = std::numeric_limits<int64_t>::max() - 1;
  audio_renderer_->Play(kLargeTimestamp, fuchsia::media::NO_TIMESTAMP,
                        AddUnexpectedCallback("Play"));

  ExpectDisconnect(audio_renderer_);
}

TEST_F(AudioRendererTransportTest, PlayWithLargeMediaTimeShouldDisconnect) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);
  CreateAndAddPayloadBuffer(0);

  // Use 1 tick per 2 frames to overflow the translation from PTS -> frames.
  audio_renderer_->SetPtsUnits(kTestStreamType.frames_per_second / 2, 1);

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));

  constexpr int64_t kLargeTimestamp = std::numeric_limits<int64_t>::max() / 2 + 1;
  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, kLargeTimestamp,
                        AddUnexpectedCallback("Play"));

  ExpectDisconnect(audio_renderer_);
}

TEST_F(AudioRendererTransportTest, PlayWithLargeNegativeMediaTimeShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  // Use 1 tick per 2 frames to overflow the translation from PTS -> frames.
  audio_renderer_->SetPtsUnits(kTestStreamType.frames_per_second / 2, 1);

  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));

  constexpr int64_t kLargeTimestamp = std::numeric_limits<int64_t>::min() + 1;
  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, kLargeTimestamp,
                        AddUnexpectedCallback("Play"));

  ExpectDisconnect(audio_renderer_);
}

// Pause stops the renderer timeline, so packets subsequently submitted should not complete.
TEST_F(AudioRendererTransportTest, Pause) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, AddCallback("Play"));
  // Ensure that the transition to Play has completed fully
  ExpectCallbacks();

  audio_renderer_->Pause(AddCallback("Pause"));
  // Ensure that the transition to Pause has completed fully
  ExpectCallbacks();

  auto packet = kTestPacket;
  packet.pts = ZX_MSEC(100);
  // If we are paused, this newly-submitted packet should not complete
  audio_renderer_->SendPacket(std::move(packet), AddUnexpectedCallback("SendPacket"));

  ExpectConnected();  // fail on disconnect or the SendPacket completion
}

// This is the sole test case to expressly target PauseNoReply, although it is used elsewhere.
TEST_F(AudioRendererTransportTest, PauseNoReply) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, AddCallback("Play"));
  ExpectCallbacks();
  audio_renderer_->PauseNoReply();

  auto packet = kTestPacket;
  packet.pts = ZX_MSEC(100);
  // If we are paused, this newly-submitted packet should not complete
  audio_renderer_->SendPacket(std::move(packet), AddUnexpectedCallback("SendPacket"));

  ExpectConnected();
}

// Without a format, Pause should not succeed.
TEST_F(AudioRendererTransportTest, PauseWithoutFormatShouldDisconnect) {
  CreateAndAddPayloadBuffer(0);

  audio_renderer_->Pause(AddUnexpectedCallback("Pause"));
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  ExpectDisconnect(audio_renderer_);
}

// Without a payload buffer, Pause should not succeed.
TEST_F(AudioRendererTransportTest, PauseWithoutBufferShouldDisconnect) {
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->Pause(AddUnexpectedCallback("Pause"));
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  ExpectDisconnect(audio_renderer_);
}

// "Quick" and "Multiple" cases validate synchronization via a series of immediate Play/Pause calls
//
// Immediate Play then Pause. Verify we are paused by failing if the packet completes
TEST_F(AudioRendererTransportTest, QuickPlayPause) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, AddCallback("Play"));
  audio_renderer_->Pause(AddCallback("Pause"));

  // If we are paused, this packet will not complete.
  audio_renderer_->SendPacket(kTestPacket, AddUnexpectedCallback("SendPacket"));

  ExpectConnected();
}

// Immediate Pause then Play. Verify we are playing by expecting the packet completion
TEST_F(AudioRendererTransportTest, QuickPausePlay) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, AddCallback("Play1"));
  ExpectCallbacks();  // Ensure we are playing before proceeding

  audio_renderer_->Pause(AddCallback("Pause"));
  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 1, AddCallback("Play"));

  // Are we playing? This packet will eventually complete, if so.
  audio_renderer_->SendPacket(kTestPacket, AddCallback("SendPacket"));

  ExpectCallbacks();
}

TEST_F(AudioRendererTransportTest, MultiplePlayPause) {
  CreateAndAddPayloadBuffer(0);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, AddCallback("Play1"));
  audio_renderer_->Pause(AddCallback("Pause1"));
  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 1, AddCallback("Play2"));
  audio_renderer_->Pause(AddCallback("Pause2"));
  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 2, AddCallback("Play3"));
  audio_renderer_->Pause(AddCallback("Pause3"));
  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 3, AddCallback("Play4"));
  audio_renderer_->Pause(AddCallback("Pause4"));

  audio_renderer_->SendPacket(kTestPacket, AddUnexpectedCallback("SendPacket"));

  ExpectConnected();
}

TEST_F(AudioRendererTransportTest, CommandsSerializedAfterPause) {
  CreateAndAddPayloadBuffer(1);
  audio_renderer_->SetPcmStreamType(kTestStreamType);

  static constexpr fuchsia::media::StreamPacket packet1{
      .payload_buffer_id = 1,
      .payload_offset = 0,
      .payload_size = kDefaultPacketSize,
  };
  static constexpr fuchsia::media::StreamPacket packet2{
      .payload_buffer_id = 2,
      .payload_offset = 0,
      .payload_size = kDefaultPacketSize,
  };

  audio_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, AddCallback("Play1"));
  audio_renderer_->Pause(AddCallback("Pause1"));
  audio_renderer_->SendPacket(packet1, AddCallback("SendPacket1"));
  audio_renderer_->DiscardAllPackets(AddCallback("DiscardAllPackets1"));
  // {Add,Remove}PayloadBuffer don't have callbacks, however they will crash
  // if not invoked in the correct order: Add will crash if the packet queue
  // is not empty (not called after the above discard) and Remove will crash
  // if not called after Add.
  CreateAndAddPayloadBuffer(2);
  audio_renderer_->SendPacket(packet2, AddCallback("SendPacket2"));
  // Queue must be empty before removing the payload buffer.
  audio_renderer_->DiscardAllPackets(AddCallback("DiscardAllPackets2"));
  audio_renderer_->RemovePayloadBuffer(2);

  ExpectCallbacks();

  // Do this after ExpectCallbacks to ensure the above callbacks have fired,
  // otherwise the ping sent by ExpectedConnect might return before some of
  // the async methods (such as SendPacket) have completed.
  ExpectConnected();
}

// Thin wrapper around AudioRendererTest for test grouping, to test BindGainControl.
class AudioRendererGainTest : public AudioRendererTest {
  // Most gain tests were moved to gain_control_test.cc. Keep this test fixture intact for now, in
  // anticipation of cases that check interactions between SetGain and Play/Pause gain-ramping.
 protected:
  void SetUp() override {
    AudioRendererTest::SetUp();

    audio_renderer_->BindGainControl(gain_control_.NewRequest());
    AddErrorHandler(gain_control_, "AudioRenderer::GainControl");

    audio_core_->CreateAudioRenderer(audio_renderer_2_.NewRequest());
    AddErrorHandler(audio_renderer_2_, "AudioRenderer2");

    audio_renderer_2_->BindGainControl(gain_control_2_.NewRequest());
    AddErrorHandler(gain_control_2_, "AudioRenderer::GainControl2");
  }

  void TearDown() override {
    gain_control_.Unbind();

    AudioRendererTest::TearDown();
  }

  fuchsia::media::audio::GainControlPtr gain_control_;
  fuchsia::media::AudioRendererPtr audio_renderer_2_;
  fuchsia::media::audio::GainControlPtr gain_control_2_;
};

// Validate AudioRenderers can create GainControl interfaces, that renderers persist after their
// gain_control is unbound, but that gain_controls do NOT persist after their renderer is unbound.
TEST_F(AudioRendererGainTest, BindGainControl) {
  // Validate gain_control_2_ does NOT persist after audio_renderer_2_ is unbound...
  audio_renderer_2_.Unbind();

  // ... but validate that audio_renderer_ DOES persist without gain_control_
  gain_control_.Unbind();

  ExpectDisconnect(gain_control_2_);

  ExpectConnected();  // Let audio_renderer_ show it is still alive (or let disconnects emerge)
}

}  // namespace media::audio::test
