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

// This class is thread hostile: none of its methods can be called concurrently.
class RendererShimImpl {
 public:
  static constexpr uint32_t kPacketMs = 10;

  ~RendererShimImpl();

  fuchsia::media::AudioRendererPtr& fidl() { return fidl_; }
  VmoBackedBuffer& payload() { return payload_buffer_; }
  const Format& format() const { return format_; }

  size_t num_packet_frames() const { return format_.frames_per_second() / 1000 * kPacketMs; }
  size_t num_packet_samples() const { return num_packet_frames() * format_.channels(); }
  size_t num_packet_bytes() const { return num_packet_frames() * format_.bytes_per_frame(); }

  size_t num_payload_frames() const { return payload_frame_count_; }
  size_t num_payload_packets() const { return payload_frame_count_ / num_packet_frames(); }
  size_t num_payload_samples() const { return payload_frame_count_ * format_.channels(); }
  size_t num_payload_bytes() const { return payload_frame_count_ * format_.bytes_per_frame(); }

  // Minimum lead time for the AudioRenderer.
  zx::duration min_lead_time() const { return min_lead_time_.value(); }

  // Sets the units used by the presentation (media) timeline.
  // By default, we use format.frames_per_second / 1, which means 1 PTS tick = 1 frame.
  // See FIDL's AudioRenderer::SetPtsUnits.
  void SetPtsUnits(uint32_t ticks_per_second_numerator, uint32_t ticks_per_second_denominator);

  // Return the time in the current reference clock that corresponds to the given monotonic time.
  zx::time ReferenceTimeFromMonotonicTime(zx::time mono_time);

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
  RendererShimImpl(Format format, size_t payload_frame_count, size_t inspect_id)
      : format_(format),
        payload_frame_count_(payload_frame_count),
        inspect_id_(inspect_id),
        payload_buffer_(format, payload_frame_count) {}

  void SetReferenceClock(TestFixture* fixture, const zx::clock& clock);
  void RetrieveReferenceClock(TestFixture* fixture);
  void ResetEvents();
  void WatchEvents();
  VmoBackedBuffer& payload_buffer() { return payload_buffer_; }
  bool has_min_lead_time() const { return min_lead_time_.has_value(); }

 private:
  const Format format_;
  const size_t payload_frame_count_;
  const size_t inspect_id_;

  zx::clock reference_clock_;

  VmoBackedBuffer payload_buffer_;
  fuchsia::media::AudioRendererPtr fidl_;
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
  AudioRendererShim(TestFixture* fixture, fuchsia::media::AudioCorePtr& audio_core, Format fmt,
                    size_t payload_frame_count, fuchsia::media::AudioRenderUsage usage,
                    size_t inspect_id, std::optional<zx::clock> reference_clock)
      : RendererShimImpl(fmt, payload_frame_count, inspect_id) {
    audio_core->CreateAudioRenderer(fidl().NewRequest());
    fixture->AddErrorHandler(fidl(), "AudioRenderer");
    WatchEvents();

    if (reference_clock) {
      SetReferenceClock(fixture, *reference_clock);
    }
    fidl()->SetUsage(usage);
    fidl()->SetPcmStreamType({.sample_format = format().sample_format(),
                              .channels = format().channels(),
                              .frames_per_second = format().frames_per_second()});

    SetPtsUnits(format().frames_per_second(), 1);
    fidl()->AddPayloadBuffer(0, payload_buffer().CreateAndMapVmo(false));
    RetrieveReferenceClock(fixture);
  }

  bool created() const { return has_min_lead_time(); }
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
                         Format fmt, size_t payload_frame_count, size_t inspect_id)
      : RendererShimImpl(fmt, payload_frame_count, inspect_id), fixture_(fixture) {
    ultrasound_factory->CreateRenderer(
        fidl().NewRequest(), [this](auto ref_clock, auto stream_type) {
          created_ = true;
          reference_clock_ = std::move(ref_clock);
          EXPECT_EQ(stream_type.sample_format, format().sample_format());
          EXPECT_EQ(stream_type.channels, format().channels());
          EXPECT_EQ(stream_type.frames_per_second, format().frames_per_second());
        });
    fixture->AddErrorHandler(fidl(), "UltrasoundRenderer");

    WatchEvents();
    SetPtsUnits(format().frames_per_second(), 1);
    fidl()->AddPayloadBuffer(0, payload_buffer().CreateAndMapVmo(false));
    RetrieveReferenceClock(fixture);
  }

  void WaitForDevice() {
    fixture_->RunLoopUntil(
        [this] { return (created_ && has_min_lead_time()) || fixture_->ErrorOccurred(); });
  }

  bool created() const { return created_ && has_min_lead_time(); }

 private:
  bool created_ = false;
  TestFixture* fixture_;
  zx::clock reference_clock_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_RENDERER_SHIM_H_
