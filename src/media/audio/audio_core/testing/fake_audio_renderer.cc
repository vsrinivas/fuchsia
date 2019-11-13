// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/testing/fake_audio_renderer.h"

#include <lib/async/cpp/time.h>

#include "src/media/audio/audio_core/audio_link_packet_source.h"
#include "src/media/audio/audio_core/audio_output.h"
#include "src/media/audio/audio_core/audio_packet_ref.h"
#include "src/media/audio/audio_core/mixer/constants.h"

namespace media::audio::testing {
namespace {

const fuchsia::media::AudioStreamType kDefaultStreamType{
    .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
    .channels = 2,
    .frames_per_second = 48000,
};

}

// static
fbl::RefPtr<FakeAudioRenderer> FakeAudioRenderer::CreateWithDefaultFormatInfo(
    async_dispatcher_t* dispatcher) {
  auto renderer = FakeAudioRenderer::Create(dispatcher);
  renderer->set_format_info(AudioRendererFormatInfo::Create(kDefaultStreamType));
  return renderer;
}

FakeAudioRenderer::FakeAudioRenderer(async_dispatcher_t* dispatcher)
    : AudioObject(AudioObject::Type::AudioRenderer),
      dispatcher_(dispatcher),
      vmo_ref_(fbl::MakeRefCounted<RefCountedVmoMapper>()) {
  zx_status_t status = vmo_ref_->CreateAndMap(2 * PAGE_SIZE, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  FX_CHECK(status == ZX_OK);
}

void FakeAudioRenderer::EnqueueAudioPacket(float sample, zx::duration duration) {
  FX_CHECK(format_info_valid());
  uint32_t frame_count = format_info()->frames_per_ns().Scale(duration.to_nsecs());

  fuchsia::media::StreamPacket packet;
  packet.pts = fuchsia::media::NO_TIMESTAMP;
  packet.payload_offset = buffer_offset_;
  packet.payload_size = format_info()->bytes_per_frame() * frame_count;
  buffer_offset_ += packet.payload_size;

  FX_CHECK(packet.payload_offset + packet.payload_size <= vmo_ref_->size());

  // Write the data to the packet buffer.
  float* samples = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(vmo_ref_->start()) +
                                            packet.payload_offset);
  auto sample_count = frame_count * 2;
  for (uint32_t i = 0; i < sample_count; ++i) {
    samples[i] = sample;
  }

  if (next_pts_ == 0) {
    zx::duration min_lead_time = FindMinLeadTime();
    auto now = async::Now(dispatcher_) + min_lead_time;
    auto frac_fps = 48000 << kPtsFractionalBits;
    auto rate = TimelineRate(frac_fps, 1000000000u);
    timeline_func_ = TimelineFunction(0, now.get(), rate);
  }

  auto packet_ref = fbl::MakeRefCounted<AudioPacketRef>(
      vmo_ref_, dispatcher_, [] {}, packet, frame_count << kPtsFractionalBits, next_pts_);
  next_pts_ = packet_ref->end_pts();
  ForEachDestLink([moved_packet = std::move(packet_ref)](auto& link) {
    AsPacketSource(link).PushToPendingQueue(moved_packet);
  });
}

zx::duration FakeAudioRenderer::FindMinLeadTime() {
  TRACE_DURATION("audio", "AudioRendererImpl::RecomputeMinLeadTime");
  zx::duration cur_lead_time;

  ForEachDestLink([&cur_lead_time](auto& link) {
    if (link.GetDest()->is_output()) {
      const auto output = fbl::RefPtr<AudioOutput>::Downcast(link.GetDest());
      cur_lead_time = std::max(cur_lead_time, output->min_lead_time());
    }
  });

  return cur_lead_time;
}

}  // namespace media::audio::testing
