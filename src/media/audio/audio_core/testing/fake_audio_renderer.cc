// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/testing/fake_audio_renderer.h"

#include <lib/async/cpp/time.h>

#include "src/media/audio/audio_core/audio_output.h"
#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/packet.h"

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
  renderer->set_format(Format::Create(kDefaultStreamType));
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
  FX_CHECK(format_valid());
  uint32_t frame_count = format()->frames_per_ns().Scale(duration.to_nsecs());
  size_t payload_offset = buffer_offset_;
  size_t payload_size = format()->bytes_per_frame() * frame_count;
  buffer_offset_ += payload_size;

  FX_CHECK(payload_offset + payload_size <= vmo_ref_->size());

  // Write the data to the packet buffer.
  float* samples =
      reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(vmo_ref_->start()) + payload_offset);
  auto sample_count = frame_count * 2;
  for (uint32_t i = 0; i < sample_count; ++i) {
    samples[i] = sample;
  }

  if (next_pts_ == FractionalFrames<int64_t>(0)) {
    zx::duration min_lead_time = FindMinLeadTime();
    auto now = async::Now(dispatcher_) + min_lead_time;
    auto frac_fps = FractionalFrames<int32_t>(48000);
    auto rate = TimelineRate(frac_fps.raw_value(), zx::sec(1).to_nsecs());
    timeline_function_->Update(TimelineFunction(0, now.get(), rate));
  }

  auto packet_ref =
      fbl::MakeRefCounted<Packet>(vmo_ref_, payload_offset, FractionalFrames<uint32_t>(frame_count),
                                  next_pts_, nullptr, nullptr);
  next_pts_ = packet_ref->end();
  for (auto& [_, packet_queue] : packet_queues_) {
    packet_queue->PushPacket(packet_ref);
  }
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

zx_status_t FakeAudioRenderer::InitializeDestLink(const fbl::RefPtr<AudioLink>& link) {
  auto queue = fbl::MakeRefCounted<PacketQueue>(*format(), timeline_function_);
  packet_queues_.insert({link.get(), queue});
  link->set_stream(std::move(queue));
  return ZX_OK;
}

void FakeAudioRenderer::CleanupDestLink(const fbl::RefPtr<AudioLink>& link) {
  auto it = packet_queues_.find(link.get());
  FX_CHECK(it != packet_queues_.end());
  packet_queues_.erase(it);
}

}  // namespace media::audio::testing
