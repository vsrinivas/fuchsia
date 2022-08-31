// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/path.h"
#include "src/media/audio/audio_core/testing/integration/hermetic_audio_realm.h"
#include "src/media/audio/audio_core/testing/integration/hermetic_audio_test.h"
#include "src/media/audio/audio_core/testing/integration/virtual_device.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/format/traits.h"
#include "src/virtualization/tests/lib/enclosed_guest.h"

namespace {

using ::fuchsia::media::AudioSampleFormat;
using ::media::audio::AudioBuffer;
using ::media::audio::AudioBufferSlice;
using ::media::audio::Format;
using ::media::audio::SampleFormatTraits;
using ::media::audio::test::HermeticAudioRealm;
using ::media::audio::test::HermeticAudioTest;
using ::media::audio::test::VirtualOutput;

constexpr int32_t kOutputFrameRate = 48000;
constexpr int32_t kStereoChannelCount = 2;
constexpr AudioSampleFormat kSampleFormat = AudioSampleFormat::FLOAT;

constexpr audio_stream_unique_id_t kOutputId = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

// TODO(fxbug.dev/87646): Consider creating a `virtio_audio_test_util` that directly communicates
// with ALSA instead to have better control over the output buffer.
constexpr char kAplayBinPath[] = "/tmp/vm_extras/aplay";

// TODO(fxbug.dev/87646): Consider creating a `virtio_audio_test_util` that directly generates this
// audio files on-the-fly.
constexpr char kTestFilePath[] = "/tmp/extras/stereo_ramp_48khz_16bit.wav";

constexpr int32_t kRampFrameCount = 65536;
constexpr int32_t kZeroPaddingFrameCount = 1024;

template <class GuestType>
class VirtioSoundGuestTest : public HermeticAudioTest {
 protected:
  void SetUp() override {
    GuestLaunchInfo guest_launch_info;
    enclosed_guest_.emplace(loop());

    HermeticAudioTest::SetTestSuiteRealmOptions([this, &guest_launch_info] {
      return HermeticAudioRealm::Options{
          .customize_realm = [this, &guest_launch_info](
                                 ::component_testing::RealmBuilder& realm_builder) -> zx_status_t {
            auto status = enclosed_guest_->BuildLaunchInfo(&guest_launch_info);
            if (status != ZX_OK) {
              return status;
            }
            enclosed_guest_->InstallInRealm(realm_builder.root(), guest_launch_info);

            using component_testing::ChildRef;
            using component_testing::Protocol;
            using component_testing::Route;

            realm_builder.AddRoute(Route{
                .capabilities = {Protocol{"fuchsia.media.Audio"}},
                .source = ChildRef{HermeticAudioRealm::kAudioCore},
                .targets = {ChildRef{"guest_manager"}},
            });
            return ZX_OK;
          },
      };
    });

    // Create the realm and start audio services.
    HermeticAudioTest::SetUp();

    // Now start the guest.
    auto services = std::make_unique<sys::ServiceDirectory>(realm().realm_root().CloneRoot());
    ASSERT_EQ(enclosed_guest_->LaunchInRealm(std::move(services), guest_launch_info,
                                             zx::time::infinite()),
              ZX_OK)
        << "Failed to launch guest";

    const auto format =
        Format::Create<kSampleFormat>(kStereoChannelCount, kOutputFrameRate).take_value();
    // Add some padding to ensure that there is enough headroom in the ring buffer.
    output_ = CreateOutput(kOutputId, format,
                           kRampFrameCount + kZeroPaddingFrameCount + 10 * kOutputFrameRate);
  }

  void TearDown() override {
    EXPECT_EQ(enclosed_guest_->Stop(zx::time::infinite()), ZX_OK);
    enclosed_guest_.reset();

    if constexpr (kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
      ExpectNoOverflowsOrUnderflows();
    }
    HermeticAudioTest::TearDown();
  }

  zx_status_t Execute(const std::vector<std::string>& argv) {
    return enclosed_guest_->Execute(argv, {}, zx::time::infinite(), nullptr, nullptr);
  }

  std::optional<int64_t> GetFirstNonSilentFrame(const AudioBuffer<kSampleFormat>& buffer) const {
    for (int64_t frame = 0; frame < buffer.NumFrames(); ++frame) {
      if (buffer.SampleAt(frame, 0) != SampleFormatTraits<kSampleFormat>::kSilentValue) {
        return frame;
      }
    }
    return std::nullopt;
  }

  AudioBuffer<kSampleFormat> GetOutputRingBuffer() { return output_->SnapshotRingBuffer(); }

  bool OutputHasUnderflows() { return DeviceHasUnderflows(output_); }

 private:
  std::optional<GuestType> enclosed_guest_;
  VirtualOutput<kSampleFormat>* output_ = nullptr;
};

// We only support `TerminaEnclosedGuest` since the tests require `virtio-sound` and `alsa-lib`.
TYPED_TEST_SUITE(VirtioSoundGuestTest, ::testing::Types<TerminaEnclosedGuest>,
                 GuestTestNameGenerator);

TYPED_TEST(VirtioSoundGuestTest, OutputFidelity) {
  // The input audio file consists of a stereo linear ramp that covers the entire 16-bit range of
  // values with opposing direction in each channel. It is calculated for each frame as follows:
  //    `buffer[frame][0] = 0x7FFF - frame`
  //    `buffer[frame][1] = -0x8000 + frame`
  // Note that the file consists of `kZeroPaddingFrameCount` frames of zeros at the beginning in
  // order to compensate for the initial gain ramp, which is then followed by the
  // `kRampFrameCount` ramp frames as described above.
  ASSERT_EQ(this->Execute({kAplayBinPath, kTestFilePath}), ZX_OK);

  const auto ring_buffer = this->GetOutputRingBuffer();
  ASSERT_EQ(ring_buffer.format().channels(), kStereoChannelCount);

  // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
  if (this->OutputHasUnderflows()) {
    GTEST_SKIP() << "Skipping fidelity checks due to underflows";
    __builtin_unreachable();
  }

  const std::optional<int64_t> start_frame = this->GetFirstNonSilentFrame(ring_buffer);
  ASSERT_TRUE(start_frame.has_value()) << "Could not find non-silent frame";
  FX_LOGS(INFO) << "First non-silent frame: " << *start_frame;

  const int64_t end_frame = *start_frame + kRampFrameCount;
  ASSERT_LE(end_frame, ring_buffer.NumFrames()) << "Not enough frames in ring buffer";

  const auto buffer_slice = AudioBufferSlice(&ring_buffer, *start_frame, end_frame);
  // TODO(fxbug.dev/95106): Temporarily limit the end frame to `24000 - kZeroPaddingFrameCount`
  // until the buffer repetition issue is resolved (to be replaced by `kRampFrameCount`).
  for (int32_t frame = 0; frame < 24000 - kZeroPaddingFrameCount; ++frame) {
    EXPECT_FLOAT_EQ(buffer_slice.SampleAt(frame, 0),
                    static_cast<float>(0x7FFF - frame) / static_cast<float>(kRampFrameCount / 2))
        << "at (" << frame << ", 0)";
    EXPECT_FLOAT_EQ(buffer_slice.SampleAt(frame, 1),
                    static_cast<float>(-0x8000 + frame) / static_cast<float>(kRampFrameCount / 2))
        << "at (" << frame << ", 1)";
  }
}

}  // namespace
