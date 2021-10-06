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

bool FakeAudioRenderer::expected() const {
  if (!expected_) {
    // A message is logged when |expected_| is set to false, so we don't log anything here.
    return false;
  }

  if (!packet_expecters_.empty()) {
    bool expecter_done = false;
    for (auto& expecter : packet_expecters_) {
      if (expecter.done()) {
        expecter_done = true;
        break;
      }
    }

    if (!expecter_done) {
      FX_LOGS(WARNING) << "Expected packets did not arrive.";
      for (auto& expecter : packet_expecters_) {
        expecter.LogExpectation();
      }

      return false;
    }
  }

  if ((delay_packet_retirement_pts_ != fuchsia::media::NO_TIMESTAMP) && !packet_queue_.empty()) {
    FX_LOGS(WARNING) << "Packet queue not empty, contains " << packet_queue_.size() << " packets.";
    return false;
  }

  return true;
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

void FakeAudioRenderer::SetReferenceClock(zx::clock ref_clock) { FX_NOTIMPLEMENTED(); }

void FakeAudioRenderer::GetReferenceClock(GetReferenceClockCallback callback) {
  callback(zx::clock(ZX_HANDLE_INVALID));
  FX_NOTIMPLEMENTED();
}

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

  if (!packet_expecters_.empty()) {
    bool expecter_ok = false;

    for (auto& expecter : packet_expecters_) {
      if (expecter.IsExpected(packet, vmo_mapper_.start())) {
        expecter_ok = true;
      }
    }

    if (!expecter_ok) {
      FX_LOGS(ERROR) << "supplied packet info { " << packet.pts << ", " << packet.payload_size
                     << ", 0x" << std::hex << std::setw(16) << std::setfill('0')
                     << PacketInfo::Hash(
                            reinterpret_cast<uint8_t*>(vmo_mapper_.start()) + packet.payload_offset,
                            packet.payload_size)
                     << std::dec << " } doesn't match expected packet(s)";
      for (auto& expecter : packet_expecters_) {
        expecter.LogExpectation();
      }

      expected_ = false;
    }
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
        if (retain_packets_ || !progressing() || packet_queue_.empty()) {
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

FakeAudioRenderer::PacketExpecter::PacketExpecter(const std::vector<PacketInfo>&& info)
    : info_(std::move(info)), iter_(info_.begin()) {}

bool FakeAudioRenderer::PacketExpecter::IsExpected(const fuchsia::media::StreamPacket& packet,
                                                   const void* start) {
  if (iter_ == info_.end()) {
    return false;
  }

  if (iter_->pts() != packet.pts || iter_->size() != packet.payload_size ||
      iter_->hash() !=
          PacketInfo::Hash(reinterpret_cast<const uint8_t*>(start) + packet.payload_offset,
                           packet.payload_size)) {
    return false;
  }

  ++iter_;
  return true;
}

void FakeAudioRenderer::PacketExpecter::LogExpectation() const {
  if (iter_ == info_.end()) {
    FX_LOGS(WARNING) << "    expected no packet";
    return;
  }

  FX_LOGS(WARNING) << "    expected { " << iter_->pts() << ", " << iter_->size() << ", 0x"
                   << std::hex << std::setw(16) << std::setfill('0') << iter_->hash() << std::dec
                   << " }";
}

}  // namespace test
}  // namespace media_player
