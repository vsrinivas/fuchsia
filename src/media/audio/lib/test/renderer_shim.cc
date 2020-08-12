// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/renderer_shim.h"

#include <algorithm>

#include "lib/zx/time.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/logging/logging.h"
#include "src/media/audio/lib/test/virtual_device.h"

namespace media::audio::test {

namespace internal {
size_t renderer_shim_next_inspect_id = 1;  // ids start at 1
}  // namespace internal

RendererShimImpl::~RendererShimImpl() { ResetEvents(); }

void RendererShimImpl::ResetEvents() {
  renderer_->EnableMinLeadTimeEvents(false);
  renderer_.events().OnMinLeadTimeChanged = nullptr;
}

void RendererShimImpl::WatchEvents() {
  renderer_->EnableMinLeadTimeEvents(true);
  renderer_.events().OnMinLeadTimeChanged = [this](int64_t min_lead_time_nsec) {
    received_min_lead_time_ = true;
    AUDIO_LOG(DEBUG) << "OnMinLeadTimeChanged: " << min_lead_time_nsec;
    min_lead_time_ = min_lead_time_nsec;
  };
}

void RendererShimImpl::SetPtsUnits(uint32_t ticks_per_second_numerator,
                                   uint32_t ticks_per_second_denominator) {
  renderer_->SetPtsUnits(ticks_per_second_numerator, ticks_per_second_denominator);
  pts_ticks_per_second_ = TimelineRate(ticks_per_second_numerator, ticks_per_second_denominator);
  pts_ticks_per_frame_ =
      TimelineRate::Product(pts_ticks_per_second_, TimelineRate(1, format_.frames_per_second()));
}

void RendererShimImpl::Play(TestFixture* fixture, zx::time reference_time, int64_t media_time) {
  renderer_->Play(
      reference_time.get(), media_time,
      fixture->AddCallback("Play", [&reference_time, &media_time](int64_t actual_reference_time,
                                                                  int64_t actual_media_time) {
        if (reference_time.get() != fuchsia::media::NO_TIMESTAMP) {
          EXPECT_EQ(reference_time.get(), actual_reference_time);
        } else {
          reference_time = zx::time(actual_reference_time);
        }
        if (media_time != fuchsia::media::NO_TIMESTAMP) {
          EXPECT_EQ(media_time, actual_media_time);
        } else {
          media_time = actual_media_time;
        }
      }));
  fixture->ExpectCallback();

  // Update the reference times for each queued packet.
  TimelineRate ns_per_pts_tick =
      TimelineRate::Product(pts_ticks_per_second_.Inverse(), TimelineRate::NsPerSecond);
  for (auto p : queued_packets_) {
    p->start_ref_time = reference_time + zx::nsec(ns_per_pts_tick.Scale(p->start_pts - media_time));
    p->end_ref_time = reference_time + zx::nsec(ns_per_pts_tick.Scale(p->end_pts - media_time));
  }
  queued_packets_.clear();
}

zx::time RendererShimImpl::PlaySynchronized(
    TestFixture* fixture, VirtualDevice<fuchsia::virtualaudio::Output>* output_device,
    int64_t media_time) {
  FX_CHECK(min_lead_time_ >= 0);
  // Synchronize at some point that is at least min_lead_time + tolerance in the future,
  // where tolerance estimates the maximum execution delay between the time we compute the
  // next synchronized time and the time we call Play.
  const auto tolerance = zx::msec(5);
  auto min_start_time = zx::clock::get_monotonic() + zx::nsec(min_lead_time_) + tolerance;
  auto mono_time = output_device->NextSynchronizedTimestamp(min_start_time);
  // TODO(fxbug.dev/46650): Translate mono_time to the renderer's reference time.
  Play(fixture, mono_time, media_time);
  return mono_time;
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
RendererShimImpl::PacketVector RendererShimImpl::AppendPackets(
    const std::vector<AudioBufferSlice<SampleFormat>>& slices, int64_t initial_pts) {
  // Where in the payload to write the next packet.
  size_t payload_offset = payload_buffer_.GetCurrentOffset();

  // Where in the media timeline to write the next packet.
  int64_t pts = initial_pts;

  PacketVector out;
  for (auto& slice : slices) {
    payload_buffer_.Append(slice);

    for (size_t frame = 0; frame < slice.NumFrames(); frame += num_packet_frames()) {
      // Every packet is kPacketMs long, except the last packet might be shorter.
      size_t num_frames = std::min(num_packet_frames(), slice.NumFrames() - frame);
      auto packet = std::make_shared<Packet>();
      packet->start_pts = pts;
      packet->end_pts = initial_pts + pts_ticks_per_frame_.Scale(frame + num_frames);
      out.push_back(packet);

      fuchsia::media::StreamPacket stream_packet{
          .pts = pts,
          .payload_offset = payload_offset,
          .payload_size = num_frames * slice.format().bytes_per_frame(),
      };

      AUDIO_LOG(TRACE) << " sending pkt at pts " << packet->start_pts << ", frame " << frame
                       << " of slice";
      renderer_->SendPacket(stream_packet, [packet]() {
        AUDIO_LOG(TRACE) << " return pkt at pts " << packet->start_pts;
        packet->returned = true;
      });

      pts = packet->end_pts;
      payload_offset += stream_packet.payload_size;
    }
  }

  queued_packets_.insert(queued_packets_.end(), out.begin(), out.end());
  return out;
}

void RendererShimImpl::WaitForPackets(TestFixture* fixture,
                                      const std::vector<std::shared_ptr<Packet>>& packets,
                                      size_t ring_out_frames) {
  FX_CHECK(!packets.empty());
  auto end_time = (*packets.rbegin())->end_ref_time;
  auto timeout = end_time - zx::clock::get_monotonic();

  // Wait until all packets are rendered AND the timeout is reached.
  // It's not sufficient to wait for just the packets, because that may not include ring_out_frames.
  // It's not sufficient to just wait for the timeout, because the SendPacket callbacks may not have
  // executed yet.
  fixture->RunLoopWithTimeout(timeout);
  fixture->RunLoopUntil([packets]() {
    for (auto& p : packets) {
      if (!p->returned) {
        return false;
      }
    }
    return true;
  });
  fixture->ExpectNoUnexpectedErrors("during WaitForPackets");
}

// Explicitly instantiate all possible implementations.
#define INSTANTIATE(T)                                                        \
  template RendererShimImpl::PacketVector RendererShimImpl::AppendPackets<T>( \
      const std::vector<AudioBufferSlice<T>>&, int64_t);

INSTANTIATE_FOR_ALL_FORMATS(INSTANTIATE)

}  // namespace media::audio::test
