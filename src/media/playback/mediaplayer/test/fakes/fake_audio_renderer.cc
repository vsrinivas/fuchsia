// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/fakes/fake_audio_renderer.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/zx/clock.h>

#include <iomanip>
#include <iostream>
#include <limits>

namespace media_player {
namespace test {

FakeAudioRenderer::FakeAudioRenderer()
    : dispatcher_(async_get_default_dispatcher()), binding_(this) {}

FakeAudioRenderer::~FakeAudioRenderer() {}

void FakeAudioRenderer::Bind(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request) {
  binding_.Bind(std::move(request));
}

void FakeAudioRenderer::SetPcmStreamType(fuchsia::media::AudioStreamType format) {
  format_ = format;
}

void FakeAudioRenderer::AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) {
  FX_DCHECK(id == 0) << "Only ID 0 is currently supported.";
  vmo_mapper_.Map(std::move(payload_buffer), 0, 0, ZX_VM_PERM_READ);
}

void FakeAudioRenderer::RemovePayloadBuffer(uint32_t id) { FX_NOTIMPLEMENTED(); }

void FakeAudioRenderer::SetPtsUnits(uint32_t tick_per_second_numerator,
                                    uint32_t tick_per_second_denominator) {
  pts_rate_ = media::TimelineRate(tick_per_second_numerator, tick_per_second_denominator);
}

void FakeAudioRenderer::SetPtsContinuityThreshold(float threshold_seconds) {
  threshold_seconds_ = threshold_seconds;
}

void FakeAudioRenderer::SetReferenceClock(zx::handle ref_clock) { FX_NOTIMPLEMENTED(); }

void FakeAudioRenderer::SendPacket(fuchsia::media::StreamPacket packet,
                                   SendPacketCallback callback) {
  packets_received_++;

  if (dump_packets_) {
    std::cerr << "{ " << packet.pts << ", " << packet.payload_size << ", 0x" << std::hex
              << std::setw(16) << std::setfill('0')
              << PacketInfo::Hash(
                     reinterpret_cast<uint8_t*>(vmo_mapper_.start()) + packet.payload_offset,
                     packet.payload_size)
              << std::dec << " },\n";
  }

  if (!expected_packets_info_.empty()) {
    if (expected_packets_info_iter_ == expected_packets_info_.end()) {
      FX_LOGS(ERROR) << "packet supplied after expected packets";
      expected_ = false;
    }

    if (expected_packets_info_iter_->pts() != packet.pts ||
        expected_packets_info_iter_->size() != packet.payload_size ||
        expected_packets_info_iter_->hash() !=
            PacketInfo::Hash(
                reinterpret_cast<uint8_t*>(vmo_mapper_.start()) + packet.payload_offset,
                packet.payload_size)) {
      FX_LOGS(ERROR) << "supplied packet doesn't match expected packet info";
      expected_ = false;
    }

    ++expected_packets_info_iter_;
  }

  packet_queue_.push(std::make_pair(packet, std::move(callback)));

  if (packet_queue_.size() == 1) {
    MaybeScheduleRetirement();
  }
}

void FakeAudioRenderer::SendPacketNoReply(fuchsia::media::StreamPacket packet) {
  SendPacket(std::move(packet), []() {});
}

void FakeAudioRenderer::EndOfStream() { FX_NOTIMPLEMENTED(); }

void FakeAudioRenderer::DiscardAllPackets(DiscardAllPacketsCallback callback) {
  while (!packet_queue_.empty()) {
    packet_queue_.front().second();
    packet_queue_.pop();
  }

  callback();
}

void FakeAudioRenderer::DiscardAllPacketsNoReply() {
  DiscardAllPackets([]() {});
}

void FakeAudioRenderer::Play(int64_t reference_time, int64_t media_time, PlayCallback callback) {
  if (vmo_mapper_.start() == nullptr) {
    FX_LOGS(ERROR) << "Play called with no buffer added";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  if (reference_time == fuchsia::media::NO_TIMESTAMP) {
    reference_time = zx::clock::get_monotonic().get();
  }

  if (media_time == fuchsia::media::NO_TIMESTAMP) {
    if (restart_media_time_ != fuchsia::media::NO_TIMESTAMP) {
      media_time = restart_media_time_;
    } else if (packet_queue_.empty()) {
      media_time = 0;
    } else {
      media_time = packet_queue_.front().first.pts;
    }
  }

  callback(reference_time, media_time);

  timeline_function_ = media::TimelineFunction(media_time, reference_time,
                                               pts_rate_ / media::TimelineRate::NsPerSecond);

  MaybeScheduleRetirement();
}

void FakeAudioRenderer::PlayNoReply(int64_t reference_time, int64_t media_time) {
  Play(reference_time, media_time, [](int64_t reference_time, int64_t media_time) {});
}

void FakeAudioRenderer::Pause(PauseCallback callback) {
  if (vmo_mapper_.start() == nullptr) {
    FX_LOGS(ERROR) << "Pause called with no buffer added";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  int64_t reference_time = zx::clock::get_monotonic().get();
  int64_t media_time = timeline_function_(reference_time);
  timeline_function_ = media::TimelineFunction(media_time, reference_time, 0, 1);
  callback(reference_time, media_time);
}

void FakeAudioRenderer::PauseNoReply() {
  Pause([](int64_t reference_time, int64_t media_time) {});
}

void FakeAudioRenderer::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) {
  FX_NOTIMPLEMENTED();
}

void FakeAudioRenderer::EnableMinLeadTimeEvents(bool enabled) {
  if (enabled) {
    binding_.events().OnMinLeadTimeChanged(min_lead_time_ns_);
  }
}

void FakeAudioRenderer::GetMinLeadTime(GetMinLeadTimeCallback callback) {
  callback(min_lead_time_ns_);
}

void FakeAudioRenderer::SetGain(float gain_db) { gain_ = gain_db; }

void FakeAudioRenderer::SetMute(bool muted) { mute_ = muted; }

void FakeAudioRenderer::MaybeScheduleRetirement() {
  if (retain_packets_ || !progressing() || packet_queue_.empty()) {
    return;
  }

  int64_t packet_pts = packet_queue_.front().first.pts;
  int64_t reference_time = timeline_function_.ApplyInverse(packet_pts);
  if (packet_pts == delay_packet_retirement_pts_) {
    reference_time += ZX_SEC(1);
  }

  async::PostTaskForTime(
      dispatcher_,
      [this]() {
        if (!progressing() || packet_queue_.empty()) {
          return;
        }

        int64_t reference_time = timeline_function_.ApplyInverse(packet_queue_.front().first.pts);

        if (reference_time <= zx::clock::get_monotonic().get()) {
          packet_queue_.front().second();
          packet_queue_.pop();
        }

        MaybeScheduleRetirement();
      },
      zx::time(reference_time));
}

}  // namespace test
}  // namespace media_player
