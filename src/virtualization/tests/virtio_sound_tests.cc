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

#include "lib/sys/cpp/testing/enclosing_environment.h"
#include "src/lib/files/path.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/format/traits.h"
#include "src/media/audio/lib/test/hermetic_audio_environment.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"
#include "src/media/audio/lib/test/virtual_device.h"
#include "src/virtualization/tests/enclosed_guest.h"

namespace {

using ::fuchsia::media::AudioSampleFormat;
using ::media::audio::AudioBuffer;
using ::media::audio::AudioBufferSlice;
using ::media::audio::Format;
using ::media::audio::SampleFormatTraits;
using ::media::audio::test::HermeticAudioEnvironment;
using ::media::audio::test::HermeticAudioTest;
using ::media::audio::test::VirtualOutput;

constexpr int32_t kOutputFrameRate = 48000;
constexpr int32_t kStereoChannelCount = 2;
constexpr AudioSampleFormat kSampleFormat = AudioSampleFormat::FLOAT;

constexpr audio_stream_unique_id_t kOutputId = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

constexpr char kEnvironmentLabel[] = "virtio-sound-guest-test";

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
 public:
  static void SetUpTestSuite() {
    // Set `HermeticAudioEnvironment` to install guest services.
    HermeticAudioEnvironment::Options options = {
        .install_additional_services_fn = [&](sys::testing::EnvironmentServices& services)
            -> zx_status_t { return GetEnclosedGuest().Install(services); },
        .label = kEnvironmentLabel};
    HermeticAudioTest::SetTestSuiteEnvironmentOptions(std::move(options));
  }

  static GuestType& GetEnclosedGuest() {
    FX_CHECK(enclosed_guest_.has_value());
    return enclosed_guest_.value();
  }

 protected:
  void SetUp() override {
    enclosed_guest_.emplace(loop());
    HermeticAudioTest::SetUp();

    const auto format =
        Format::Create<kSampleFormat>(kStereoChannelCount, kOutputFrameRate).take_value();
    // Add some padding to ensure that there is enough headroom in the ring buffer.
    output_ = CreateOutput(kOutputId, format,
                           kRampFrameCount + kZeroPaddingFrameCount + 4 * kOutputFrameRate);

    ASSERT_EQ(GetEnclosedGuest().Launch(environment()->GetEnvironment(), kEnvironmentLabel,
                                        zx::time::infinite()),
              ZX_OK)
        << "Failed to launch guest";
  }

  void TearDown() override {
    EXPECT_EQ(GetEnclosedGuest().Stop(zx::time::infinite()), ZX_OK);
    enclosed_guest_.reset();

    if constexpr (kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
      ExpectNoOverflowsOrUnderflows();
    }
    HermeticAudioTest::TearDown();
  }

  zx_status_t Execute(const std::vector<std::string>& argv) {
    return GetEnclosedGuest().Execute(argv, {}, zx::time::infinite(), nullptr, nullptr);
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

 private:
  static inline std::optional<GuestType> enclosed_guest_;

  VirtualOutput<kSampleFormat>* output_ = nullptr;
};

// We only support `TerminaEnclosedGuest` since the tests require `virtio-sound` and `alsa-lib`.
TYPED_TEST_SUITE(VirtioSoundGuestTest, ::testing::Types<TerminaEnclosedGuest>);

TYPED_TEST(VirtioSoundGuestTest, OutputFidelity) {
  // The input audio file consists of a stereo linear ramp that covers the entire 16-bit range of
  // values with opposing direction in each channel. It is calculated for each frame as follows:
  //    `buffer[frame][0] = 0x7FFF - frame`
  //    `buffer[frame][1] = -0x8000 + frame`
  // Note that the file consists of `kZeroPaddingFrameCount` frames of zeros at the beginning in
  // order to compensate for the initial gain ramp, which is then followed by the `kRampFrameCount`
  // ramp frames as described above.
  ASSERT_EQ(this->Execute({kAplayBinPath, kTestFilePath}), ZX_OK);

  const auto ring_buffer = this->GetOutputRingBuffer();
  ASSERT_EQ(ring_buffer.format().channels(), kStereoChannelCount);

  const std::optional<int64_t> start_frame = this->GetFirstNonSilentFrame(ring_buffer);
  ASSERT_TRUE(start_frame.has_value()) << "Could not find non-silent frame";

  const int64_t end_frame = *start_frame + kRampFrameCount;
  ASSERT_LE(end_frame, ring_buffer.NumFrames()) << "Not enough frames in ring buffer";

  const auto buffer_slice = AudioBufferSlice(&ring_buffer, *start_frame, end_frame);
  // TODO(fxbug.dev/94467): Temporarily limit the end frame to `24000 - kZeroPaddingFrameCount`
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
