// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_RENDERER_SHIM_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_RENDERER_SHIM_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/ultrasound/cpp/fidl.h>
#include <lib/zx/clock.h>

#include <memory>
#include <vector>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/test/test_fixture.h"
#include "src/media/audio/lib/test/vmo_backed_buffer.h"

namespace media::audio::test {

template <class Interface>
class VirtualDevice;

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
  zx::duration GetMinLeadTime() const { return min_lead_time_.value(); }

  // Sets the units used by the presentation (media) timeline.
  // By default, we use format.frames_per_second / 1, which means 1 PTS tick = 1 frame.
  void SetPtsUnits(uint32_t ticks_per_second_numerator, uint32_t ticks_per_second_denominator);

  // Send a Play command to the renderer and wait until it is processed.
  // Either time may be NO_TIMESTAMP, as described in the FIDL documentation.
  void Play(TestFixture* fixture, zx::time reference_time, int64_t media_time);

  // Like Play, but aligns the reference_time with the start of output_device's ring buffer.
  // Returns the reference_time at which the audio will start playing.
  zx::time PlaySynchronized(TestFixture* fixture,
                            VirtualDevice<fuchsia::virtualaudio::Output>* output_device,
                            int64_t media_time);

  struct Packet {
    // The packet spans timestamps [start_pts, end_pts), so end_pts is the start_pts of the
    // next contiguous packet. By default, unless overriden by SetPtsUnits, 1 PTS = 1 frame.
    int64_t start_pts;
    int64_t end_pts;
    zx::time start_ref_time;  // reference time corresponding to start_pts (set by Play)
    zx::time end_ref_time;    // reference time corresponding to end_pts (set by Play)
    bool returned = false;    // set after the packet was returned from AudioCore
  };

  using PacketVector = std::vector<std::shared_ptr<Packet>>;

  // Submit the given slices as a sequence of timestamped packets of length at most kPacketMs.
  // The packets are appended to the payload buffer after the last call to ClearPayload().
  template <fuchsia::media::AudioSampleFormat SampleFormat>
  PacketVector AppendPackets(const std::vector<AudioBufferSlice<SampleFormat>>& slices,
                             int64_t initial_pts = 0);

  // Wait until the given packets are rendered. |packets| must be non-empty and must be ordered by
  // start_pts. If |ring_out_frames| > 0, we wait for all |packets| to be rendered, plus an
  // additional |ring_out_frames|.
  void WaitForPackets(TestFixture* fixture, const PacketVector& packets,
                      size_t ring_out_frames = 0);

  // Reset the payload buffer to all zeros and seek back to the start.
  void ClearPayload() { payload_buffer_.Clear(); }

  // For validating properties exported by inspect.
  size_t inspect_id() const { return inspect_id_; }

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
  std::optional<zx::duration> min_lead_time_;
  TimelineRate pts_ticks_per_second_;
  TimelineRate pts_ticks_per_frame_;
  PacketVector queued_packets_;
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
    fixture->AddErrorHandler(renderer_, "AudioRenderer");
    WatchEvents();

    renderer_->SetUsage(usage);
    renderer_->SetPcmStreamType({.sample_format = format_.sample_format(),
                                 .channels = format_.channels(),
                                 .frames_per_second = format_.frames_per_second()});

    SetPtsUnits(format_.frames_per_second(), 1);
    renderer_->AddPayloadBuffer(0, payload_buffer_.CreateAndMapVmo(false));
  }

  bool created() const { return min_lead_time_.has_value(); }
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
      : RendererShimImpl(format, payload_frame_count), fixture_(fixture) {
    ultrasound_factory->CreateRenderer(
        renderer_.NewRequest(), [this](auto ref_clock, auto stream_type) {
          created_ = true;
          reference_clock_ = std::move(ref_clock);
          EXPECT_EQ(stream_type.sample_format, format_.sample_format());
          EXPECT_EQ(stream_type.channels, format_.channels());
          EXPECT_EQ(stream_type.frames_per_second, format_.frames_per_second());
        });
    fixture->AddErrorHandler(renderer_, "UltrasoundRenderer");

    WatchEvents();
    SetPtsUnits(format_.frames_per_second(), 1);
    renderer_->AddPayloadBuffer(0, payload_buffer_.CreateAndMapVmo(false));
  }

  void WaitForDevice() {
    fixture_->RunLoopUntil(
        [this] { return (created_ && min_lead_time_.has_value()) || fixture_->ErrorOccurred(); });
  }

  bool created() const { return created_ && min_lead_time_; }

 private:
  bool created_ = false;
  TestFixture* fixture_;
  zx::clock reference_clock_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_RENDERER_SHIM_H_
