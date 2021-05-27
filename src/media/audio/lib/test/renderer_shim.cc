// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/renderer_shim.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <utility>

#include "lib/zx/time.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/test/virtual_device.h"

namespace media::audio::test {

RendererShimImpl::RendererShimImpl(Format format, int64_t payload_frame_count, size_t inspect_id)
    : format_(format),
      payload_frame_count_(payload_frame_count),
      inspect_id_(inspect_id),
      payload_buffer_(format, payload_frame_count) {
  // Start with a single slot covering the entire payload.
  // We'll subdivide this later.
  payload_slots_.push_back({
      .start_offset = 0,
      .end_offset = payload_buffer_.SizeBytes(),
  });
}

void RendererShimImpl::WatchEvents() {
  fidl_->EnableMinLeadTimeEvents(true);
  fidl_.events().OnMinLeadTimeChanged = [this](int64_t min_lead_time_nsec) {
    FX_LOGS(DEBUG) << "OnMinLeadTimeChanged: " << min_lead_time_nsec;
    // Sometimes, this can be invoked before the Renderer is actually linked.
    // When that happens, the reported lead time is zero as it hasn't been computed yet.
    // Wait until the renderer is linked before updating our lead time.
    if (min_lead_time_nsec > 0) {
      min_lead_time_ = zx::nsec(min_lead_time_nsec);
    }
  };
}

void RendererShimImpl::SetPtsUnits(uint32_t ticks_per_second_numerator,
                                   uint32_t ticks_per_second_denominator) {
  fidl_->SetPtsUnits(ticks_per_second_numerator, ticks_per_second_denominator);
  pts_ticks_per_second_ = TimelineRate(ticks_per_second_numerator, ticks_per_second_denominator);
  pts_ticks_per_frame_ =
      TimelineRate::Product(pts_ticks_per_second_, TimelineRate(1, format_.frames_per_second()));
}

void RendererShimImpl::SetReferenceClock(TestFixture* fixture, const zx::clock& clock) {
  if (clock.is_valid()) {
    auto dup_result = ::media::audio::clock::DuplicateClock(clock);
    ASSERT_TRUE(dup_result.is_ok());
    fidl()->SetReferenceClock(dup_result.take_value());
  } else {
    fidl()->SetReferenceClock(zx::clock());
  }

  RetrieveReferenceClock(fixture);
}

void RendererShimImpl::RetrieveReferenceClock(TestFixture* fixture) {
  bool done = false;
  fidl_->GetReferenceClock([this, &done](zx::clock c) {
    done = true;
    reference_clock_ = std::move(c);
  });
  fixture->RunLoopUntil([&done]() { return done; });
}

zx::time RendererShimImpl::ReferenceTimeFromMonotonicTime(zx::time mono_time) {
  return ::media::audio::clock::ReferenceTimeFromMonotonicTime(reference_clock_, mono_time).value();
}

void RendererShimImpl::Play(TestFixture* fixture, zx::time reference_time, int64_t media_time) {
  fidl_->Play(
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
  fixture->ExpectCallbacks();

  // Update the reference times for each in-flight packet.
  TimelineRate ns_per_pts_tick =
      TimelineRate::Product(pts_ticks_per_second_.Inverse(), TimelineRate::NsPerSecond);
  for (auto& p : packets_) {
    p->start_ref_time = reference_time + zx::nsec(ns_per_pts_tick.Scale(p->start_pts - media_time));
    p->end_ref_time = reference_time + zx::nsec(ns_per_pts_tick.Scale(p->end_pts - media_time));
  }
}

zx::time RendererShimImpl::PlaySynchronized(
    TestFixture* fixture, VirtualDevice<fuchsia::virtualaudio::Output>* output_device,
    int64_t media_time) {
  // Synchronize at some point that is at least min_lead_time + tolerance in the future,
  // where tolerance estimates the maximum execution delay between the time we compute the
  // next synchronized time and the time we call Play.
  const auto tolerance = zx::msec(5);
  auto min_start_time = zx::clock::get_monotonic() + *min_lead_time_ + tolerance;
  auto reference_time =
      ReferenceTimeFromMonotonicTime(output_device->NextSynchronizedTimestamp(min_start_time));
  Play(fixture, reference_time, media_time);
  return reference_time;
}

std::pair<int64_t, int64_t> RendererShimImpl::Pause(TestFixture* fixture) {
  zx_time_t ref_clock_now;
  auto status = reference_clock_.read(&ref_clock_now);
  EXPECT_EQ(status, ZX_OK);

  int64_t pause_ref_time = -1;
  int64_t pause_media_time = -1;

  fidl_->Pause(fixture->AddCallback("Pause", [ref_clock_now, &pause_ref_time, &pause_media_time](
                                                 int64_t reference_time, int64_t media_time) {
    EXPECT_GT(reference_time, ref_clock_now);

    pause_ref_time = reference_time;
    pause_media_time = media_time;
  }));
  fixture->ExpectCallbacks();

  // Now do something with these clock values.
  return std::make_pair(pause_ref_time, pause_media_time);
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
RendererShimImpl::PacketVector RendererShimImpl::AppendPackets(
    const std::vector<AudioBufferSlice<SampleFormat>>& slices, int64_t initial_pts) {
  // Where in the media timeline (in PTS units; frames by default) to write the next packet.
  int64_t pts = initial_pts;

  PacketVector out;
  for (auto& slice : slices) {
    auto pts_ticks = pts_ticks_per_frame_.Scale(slice.NumFrames());

    auto packet = std::make_shared<Packet>();
    packet->start_pts = pts;
    packet->end_pts = pts + pts_ticks;
    pts += pts_ticks;

    out.push_back(packet);
    packets_.insert(packet);
    pending_packets_.push_back({
        .packet = packet,
        .slice_start_frame = slice.start_frame(),
        .slice_bytes = std::vector<uint8_t>(slice.NumBytes()),
    });

    // Copy the slice data.
    memmove(&pending_packets_.back().slice_bytes[0],
            &slice.buf()->samples()[slice.SampleIndex(0, 0)], slice.NumBytes());
  }

  SendPendingPackets();
  return out;
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
RendererShimImpl::PacketVector RendererShimImpl::AppendSlice(AudioBufferSlice<SampleFormat> slice,
                                                             int64_t frames_per_packet,
                                                             int64_t initial_pts) {
  // Split into packets.
  std::vector<AudioBufferSlice<SampleFormat>> packets;
  for (auto f = 0; f < slice.NumFrames(); f += frames_per_packet) {
    auto start = slice.start_frame() + f;
    packets.push_back({slice.buf(), start, start + frames_per_packet});
  }
  return AppendPackets(packets, initial_pts);
}

void RendererShimImpl::SendPendingPackets() {
  while (!pending_packets_.empty()) {
    auto pp = pending_packets_.front();
    auto slot = AllocSlotFor(pp);
    if (!slot) {
      return;
    }
    pending_packets_.pop_front();

    FX_LOGS(TRACE) << " sending pkt at pts " << pp.packet->start_pts << ", frame "
                   << pp.slice_start_frame << ", to payload offset " << slot->start_offset;

    FX_CHECK(static_cast<int64_t>(pp.slice_bytes.size()) == slot->size())
        << "Expected slot of size " << pp.slice_bytes.size() << ", got slot of size "
        << slot->size();

    // Send this packet.
    payload_buffer_.WriteRawBytesAt(slot->start_offset, pp.slice_bytes);

    fuchsia::media::StreamPacket stream_packet{
        .pts = pp.packet->start_pts,
        .payload_offset = static_cast<uint64_t>(slot->start_offset),
        .payload_size = pp.slice_bytes.size(),
    };

    fidl_->SendPacket(stream_packet, [this, packet = pp.packet, slot]() {
      FX_LOGS(TRACE) << " return pkt at pts " << packet->start_pts;
      packet->returned = true;
      packets_.erase(packet);
      payload_slots_.push_back(*slot);
      SendPendingPackets();
    });
  }
}

std::optional<RendererShimImpl::PayloadSlot> RendererShimImpl::AllocSlotFor(
    const PendingPacket& pp) {
  const auto needed_bytes = static_cast<int64_t>(pp.slice_bytes.size());
  // This naive algorithm looks for the first slot large enough for the request.
  // If the slot is too big, it's split in two. This works well when all packets
  // have the same size, which is our expected use case.
  for (size_t k = 0; k < payload_slots_.size(); k++) {
    if (payload_slots_[k].size() < needed_bytes) {
      continue;
    }
    auto new_end = payload_slots_[k].start_offset + needed_bytes;
    auto out = PayloadSlot{
        .start_offset = payload_slots_[k].start_offset,
        .end_offset = new_end,
    };
    if (new_end == payload_slots_[k].end_offset) {
      payload_slots_.erase(payload_slots_.begin() + k);
    } else {
      payload_slots_[k].start_offset = new_end;
    }
    return out;
  }

  return std::nullopt;
}

void RendererShimImpl::WaitForPackets(TestFixture* fixture,
                                      const std::vector<std::shared_ptr<Packet>>& packets,
                                      int64_t ring_out_frames) {
  FX_CHECK(!packets.empty());
  auto end_time_reference = (*packets.rbegin())->end_ref_time;
  auto end_time_mono =
      ::media::audio::clock::MonotonicTimeFromReferenceTime(reference_clock_, end_time_reference)
          .value();
  auto timeout = end_time_mono - zx::clock::get_monotonic();

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
#define INSTANTIATE(T)                                                                          \
  template RendererShimImpl::PacketVector RendererShimImpl::AppendPackets<T>(                   \
      const std::vector<AudioBufferSlice<T>>&, int64_t);                                        \
  template RendererShimImpl::PacketVector RendererShimImpl::AppendSlice<T>(AudioBufferSlice<T>, \
                                                                           int64_t, int64_t);

INSTANTIATE_FOR_ALL_FORMATS(INSTANTIATE)

}  // namespace media::audio::test
