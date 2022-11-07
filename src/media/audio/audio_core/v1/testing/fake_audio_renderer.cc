// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/testing/fake_audio_renderer.h"

#include <lib/async/cpp/time.h>

#include "src/media/audio/audio_core/shared/mixer/constants.h"
#include "src/media/audio/audio_core/v1/audio_output.h"
#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/packet.h"
#include "src/media/audio/lib/clock/clone_mono.h"

namespace media::audio::testing {
namespace {

const fuchsia::media::AudioStreamType kDefaultStreamType{
    .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
    .channels = 2,
    .frames_per_second = 48000,
};

}

// static
std::shared_ptr<FakeAudioRenderer> FakeAudioRenderer::CreateWithDefaultFormatInfo(
    async_dispatcher_t* dispatcher, LinkMatrix* link_matrix,
    std::shared_ptr<AudioCoreClockFactory> clock_factory) {
  auto format_result = Format::Create(kDefaultStreamType);
  FX_CHECK(format_result.is_ok());
  return FakeAudioRenderer::Create(dispatcher, {format_result.value()},
                                   fuchsia::media::AudioRenderUsage::MEDIA, link_matrix,
                                   std::move(clock_factory));
}

FakeAudioRenderer::FakeAudioRenderer(async_dispatcher_t* dispatcher, std::optional<Format> format,
                                     fuchsia::media::AudioRenderUsage usage,
                                     LinkMatrix* link_matrix,
                                     std::shared_ptr<AudioCoreClockFactory> clock_factory)
    : AudioObject(AudioObject::Type::AudioRenderer),
      dispatcher_(dispatcher),
      format_(format),
      usage_(usage),
      packet_factory_(dispatcher, *format, 2 * zx_system_get_page_size()),
      link_matrix_(*link_matrix),
      clock_factory_(std::move(clock_factory)) {}

void FakeAudioRenderer::EnqueueAudioPacket(float sample, zx::duration duration,
                                           fit::closure callback) {
  FX_CHECK(format_valid());

  auto packet_ref = packet_factory_.CreatePacket(sample, duration, std::move(callback));
  if (packet_ref->start() == Fixed(0)) {
    zx::duration min_lead_time = FindMinLeadTime();
    auto now = async::Now(dispatcher_) + min_lead_time;
    auto frac_fps = Fixed(format()->frames_per_second());
    auto rate = TimelineRate(frac_fps.raw_value(), zx::sec(1).to_nsecs());
    timeline_function_->Update(TimelineFunction(0, now.get(), rate));
  }

  for (auto& [_, packet_queue] : packet_queues_) {
    packet_queue->PushPacket(packet_ref);
  }
}

zx::duration FakeAudioRenderer::FindMinLeadTime() {
  TRACE_DURATION("audio", "BaseRenderer::RecomputeMinLeadTime");
  zx::duration cur_lead_time;

  link_matrix_.ForEachDestLink(*this, [&cur_lead_time](LinkMatrix::LinkHandle link) {
    if (link.object->is_output()) {
      const auto& output = static_cast<const AudioOutput&>(*link.object);
      cur_lead_time = std::max(cur_lead_time, output.presentation_delay());
    }
  });

  return cur_lead_time;
}

fpromise::result<std::shared_ptr<ReadableStream>, zx_status_t>
FakeAudioRenderer::InitializeDestLink(const AudioObject& dest) {
  auto queue = std::make_shared<PacketQueue>(
      *format(), timeline_function_,
      clock_factory_->CreateClientAdjustable(clock::AdjustableCloneOfMonotonic()));
  packet_queues_.insert({&dest, queue});
  return fpromise::ok(std::move(queue));
}

void FakeAudioRenderer::CleanupDestLink(const AudioObject& dest) {
  auto it = packet_queues_.find(&dest);
  FX_CHECK(it != packet_queues_.end());
  packet_queues_.erase(it);
}

}  // namespace media::audio::testing
