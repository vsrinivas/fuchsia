// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_RENDERER_SHIM_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_RENDERER_SHIM_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/ultrasound/cpp/fidl.h>

#include <memory>
#include <vector>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/test/inspect.h"
#include "src/media/audio/lib/test/test_fixture.h"
#include "src/media/audio/lib/test/vmo_backed_buffer.h"

namespace media::audio::test {

namespace internal {
// These IDs are scoped to the lifetime of this process.
extern size_t renderer_shim_next_inspect_id;
}  // namespace internal

// This class is thread hostile: none of its methods can be called concurrently.
class RendererShimImpl {
 public:
  static constexpr uint32_t kPacketMs = 10;

  ~RendererShimImpl();

  fuchsia::media::AudioRendererPtr& renderer() { return renderer_; }
  VmoBackedBuffer& payload() { return payload_buffer_; }

  size_t num_packet_frames() const { return format_.frames_per_second() / 1000 * kPacketMs; }
  size_t num_packet_samples() const { return num_packet_frames() * format_.channels(); }
  size_t num_packet_bytes() const { return num_packet_frames() * format_.bytes_per_frame(); }

  size_t num_payload_frames() const { return payload_frame_count_; }
  size_t num_payload_packets() const { return payload_frame_count_ / num_packet_frames(); }
  size_t num_payload_samples() const { return payload_frame_count_ * format_.channels(); }
  size_t num_payload_bytes() const { return payload_frame_count_ * format_.bytes_per_frame(); }

  // Minimum lead time for the AudioRenderer.
  int64_t GetMinLeadTime() const { return min_lead_time_; }

  // Sets the units used by the presentation (media) timeline.
  // By default, we use format.frames_per_second / 1, which means 1 PTS tick = 1 frame.
  void SetPtsUnits(uint32_t ticks_per_second_numerator, uint32_t ticks_per_second_denominator);

  // Send a Play command to the renderer and wait until it is processed.
  void Play(TestFixture* fixture, int64_t reference_time, int64_t media_time);

  struct Packet {
    int64_t start_pts;      // starting frame of the packet (PTS has units frames)
    int64_t end_pts;        // one past the last frame of the packet
    bool returned = false;  // set after the packet was returned from AudioCore
  };

  using PacketVector = std::vector<std::shared_ptr<Packet>>;

  // Submit the given slices as a sequence of timestamped packets of length at most kPacketMs.
  // The packets are appended to the payload buffer after the last call to ClearPayload().
  template <fuchsia::media::AudioSampleFormat SampleFormat>
  PacketVector AppendPackets(const std::vector<AudioBufferSlice<SampleFormat>>& slices,
                             int64_t initial_pts = 0);

  // Wait until the given packets are rendered. |packets| must be non-empty and must be ordered by
  // start_pts. The reference_time refers to the time at which the first frame of packets[0] should
  // be rendered, using the same clock as the reference_time passed to Play().
  //
  // If |ring_out_frames| > 0, we wait for all |packets| to be rendered, plus an additional
  // |ring_out_frames|.
  void WaitForPackets(TestFixture* fixture, int64_t reference_time, const PacketVector& packets,
                      size_t ring_out_frames = 0);

  // Reset the payload buffer to all zeros and seek back to the start.
  void ClearPayload() { payload_buffer_.Clear(); }

  // For validating properties exported by inspect.
  // By default, there are no expectations.
  size_t inspect_id() const { return inspect_id_; }
  ExpectedInspectProperties& expected_inspect_properties() { return expected_inspect_properties_; }

 protected:
  RendererShimImpl(Format format, size_t payload_frame_count)
      : format_(format),
        payload_frame_count_(payload_frame_count),
        inspect_id_(internal::renderer_shim_next_inspect_id++),
        payload_buffer_(format, payload_frame_count) {}

  void ResetEvents();
  void WatchEvents();

  const Format format_;
  const size_t payload_frame_count_;
  const size_t inspect_id_;

  VmoBackedBuffer payload_buffer_;
  fuchsia::media::AudioRendererPtr renderer_;
  bool received_min_lead_time_ = false;
  int64_t min_lead_time_ = -1;
  TimelineRate pts_ticks_per_second_;
  TimelineRate pts_ticks_per_frame_;

  ExpectedInspectProperties expected_inspect_properties_;
};

template <fuchsia::media::AudioSampleFormat SampleFormat>
class AudioRendererShim : public RendererShimImpl {
 public:
  using SampleT = typename AudioBuffer<SampleFormat>::SampleT;

  PacketVector AppendPackets(const std::vector<AudioBufferSlice<SampleFormat>>& slices,
                             int64_t initial_pts = 0) {
    return RendererShimImpl::AppendPackets<SampleFormat>(slices, initial_pts);
  }

  // Don't call this directly. Use HermeticAudioTest::CreateAudioRenderer so the object is
  // appropriately bound into the test environment.
  AudioRendererShim(TestFixture* fixture, fuchsia::media::AudioCorePtr& audio_core, Format format,
                    size_t payload_frame_count, fuchsia::media::AudioRenderUsage usage)
      : RendererShimImpl(format, payload_frame_count) {
    audio_core->CreateAudioRenderer(renderer_.NewRequest());
    renderer_.set_error_handler(fixture->ErrorHandler());
    WatchEvents();

    renderer_->SetUsage(usage);
    renderer_->SetPcmStreamType({.sample_format = format_.sample_format(),
                                 .channels = format_.channels(),
                                 .frames_per_second = format_.frames_per_second()});

    SetPtsUnits(format_.frames_per_second(), 1);
    renderer_->AddPayloadBuffer(0, payload_buffer_.CreateAndMapVmo(false));
  }
};

template <fuchsia::media::AudioSampleFormat SampleFormat>
class UltrasoundRendererShim : public RendererShimImpl {
 public:
  using SampleT = typename AudioBuffer<SampleFormat>::SampleT;

  const zx::clock& reference_clock() const { return reference_clock_; }

  PacketVector AppendPackets(const std::vector<AudioBufferSlice<SampleFormat>>& slices,
                             int64_t initial_pts = 0) {
    return RendererShimImpl::AppendPackets<SampleFormat>(slices, initial_pts);
  }

  // Don't call this directly. Use HermeticAudioTest::CreateUltrasoundRenderer so the object is
  // appropriately bound into the test environment.
  UltrasoundRendererShim(TestFixture* fixture, fuchsia::ultrasound::FactoryPtr& ultrasound_factory,
                         Format format, size_t payload_frame_count)
      : RendererShimImpl(format, payload_frame_count) {
    bool created = false;
    ultrasound_factory->CreateRenderer(
        renderer_.NewRequest(), [this, &created](auto ref_clock, auto stream_type) {
          created = true;
          reference_clock_ = std::move(ref_clock);
          EXPECT_EQ(stream_type.sample_format, format_.sample_format());
          EXPECT_EQ(stream_type.channels, format_.channels());
          EXPECT_EQ(stream_type.frames_per_second, format_.frames_per_second());
        });
    renderer_.set_error_handler(fixture->ErrorHandler());
    fixture->RunLoopUntil([&created] { return created; });

    WatchEvents();
    SetPtsUnits(format_.frames_per_second(), 1);
    renderer_->AddPayloadBuffer(0, payload_buffer_.CreateAndMapVmo(false));
  }

 private:
  zx::clock reference_clock_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_RENDERER_SHIM_H_
