// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_CAPTURER_SHIM_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_CAPTURER_SHIM_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/ultrasound/cpp/fidl.h>

#include <memory>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/test/test_fixture.h"
#include "src/media/audio/lib/test/vmo_backed_buffer.h"

namespace media::audio::test {

// This class is thread hostile: none of its methods can be called concurrently.
class CapturerShimImpl {
 public:
  static constexpr uint32_t kPacketMs = 10;

  ~CapturerShimImpl() {}

  fuchsia::media::AudioCapturerPtr& fidl() { return fidl_; }
  VmoBackedBuffer& payload() { return payload_buffer_; }

  size_t num_payload_frames() const { return payload_frame_count_; }
  size_t num_payload_samples() const { return payload_frame_count_ * format_.channels(); }
  size_t num_payload_bytes() const { return payload_frame_count_ * format_.bytes_per_frame(); }

  // For validating properties exported by inspect.
  size_t inspect_id() const { return inspect_id_; }

 protected:
  CapturerShimImpl(Format format, size_t payload_frame_count, size_t inspect_id)
      : format_(format),
        payload_frame_count_(payload_frame_count),
        inspect_id_(inspect_id),
        payload_buffer_(format, payload_frame_count) {}

  void CreatePayloadBuffer();

  const Format format_;
  const size_t payload_frame_count_;
  const size_t inspect_id_;

  fuchsia::media::AudioCapturerPtr fidl_;
  VmoBackedBuffer payload_buffer_;
};

template <fuchsia::media::AudioSampleFormat SampleFormat>
class AudioCapturerShim : public CapturerShimImpl {
 public:
  using SampleT = typename AudioBuffer<SampleFormat>::SampleT;

  // Snapshot the current payload.
  AudioBuffer<SampleFormat> SnapshotPayload() { return payload_buffer_.Snapshot<SampleFormat>(); }
  AudioBuffer<SampleFormat> SnapshotPacket(const fuchsia::media::StreamPacket& p) {
    return payload_buffer_.SnapshotSlice<SampleFormat>(p.payload_offset, p.payload_size);
  }

  // Don't call this directly. Use HermeticAudioTest::CreateAudioCapturer so the object is
  // appropriately bound into the test environment.
  AudioCapturerShim(TestFixture* fixture, fuchsia::media::AudioCorePtr& audio_core, Format format,
                    size_t payload_frame_count, fuchsia::media::AudioCapturerConfiguration config,
                    size_t inspect_id)
      : CapturerShimImpl(format, payload_frame_count, inspect_id) {
    audio_core->CreateAudioCapturerWithConfiguration(format.stream_type(), std::move(config),
                                                     fidl_.NewRequest());
    fixture->AddErrorHandler(fidl_, "AudioCapturer");

    fidl_->SetPcmStreamType({.sample_format = format_.sample_format(),
                             .channels = format_.channels(),
                             .frames_per_second = format_.frames_per_second()});
    fidl_->AddPayloadBuffer(0, payload_buffer_.CreateAndMapVmo(true));
  }
};

template <fuchsia::media::AudioSampleFormat SampleFormat>
class UltrasoundCapturerShim : public CapturerShimImpl {
 public:
  using SampleT = typename AudioBuffer<SampleFormat>::SampleT;

  const zx::clock& reference_clock() const { return reference_clock_; }

  // Snapshot the current payload.
  AudioBuffer<SampleFormat> SnapshotPayload() { return payload_buffer_.Snapshot<SampleFormat>(); }
  AudioBuffer<SampleFormat> SnapshotPacket(const fuchsia::media::StreamPacket& p) {
    return payload_buffer_.SnapshotSlice<SampleFormat>(p.payload_offset, p.payload_size);
  }

  // Don't call this directly. Use HermeticAudioTest::CreateUltrasoundCapturer so the object is
  // appropriately bound into the test environment.
  UltrasoundCapturerShim(TestFixture* fixture, fuchsia::ultrasound::FactoryPtr& ultrasound_factory,
                         Format format, size_t payload_frame_count, size_t inspect_id)
      : CapturerShimImpl(format, payload_frame_count, inspect_id), fixture_(fixture) {
    auto vmo = payload_buffer_.CreateAndMapVmo(true);
    ultrasound_factory->CreateCapturer(
        fidl_.NewRequest(), [this, vmo = std::move(vmo)](auto ref_clock, auto stream_type) mutable {
          created_ = true;
          reference_clock_ = std::move(ref_clock);
          EXPECT_EQ(stream_type.sample_format, format_.sample_format());
          EXPECT_EQ(stream_type.channels, format_.channels());
          EXPECT_EQ(stream_type.frames_per_second, format_.frames_per_second());
          // TODO(fxbug.dev/55243): Enable AddPayloadBuffer before the capturer is created.
          fidl_->AddPayloadBuffer(0, std::move(vmo));
        });
    fixture->AddErrorHandler(fidl_, "UltrasoundCapturer");
  }

  void WaitForDevice() {
    fixture_->RunLoopUntil([this] { return created_ || fixture_->ErrorOccurred(); });
  }

  bool created() const { return created_; }

 private:
  bool created_ = false;
  TestFixture* fixture_;
  zx::clock reference_clock_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_CAPTURER_SHIM_H_
