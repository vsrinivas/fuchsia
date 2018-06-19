// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/test/fakes/fake_audio_renderer.h"

#include <iomanip>
#include <iostream>
#include <limits>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"

namespace media_player {
namespace test {

FakeAudioRenderer::FakeAudioRenderer()
    : async_(async_get_default()), binding_(this) {}

FakeAudioRenderer::~FakeAudioRenderer() {}

void FakeAudioRenderer::Bind(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer2> renderer) {
  binding_.Bind(std::move(renderer));
}

void FakeAudioRenderer::SetPcmFormat(fuchsia::media::AudioPcmFormat format) {
  format_ = format;
}

void FakeAudioRenderer::SetPayloadBuffer(::zx::vmo payload_buffer) {
  mapped_buffer_.InitFromVmo(std::move(payload_buffer), ZX_VM_FLAG_PERM_READ);
}

void FakeAudioRenderer::SetPtsUnits(uint32_t tick_per_second_numerator,
                                    uint32_t tick_per_second_denominator) {
  pts_rate_ = media::TimelineRate(tick_per_second_numerator,
                                  tick_per_second_denominator);
}

void FakeAudioRenderer::SetPtsContinuityThreshold(float threshold_seconds) {
  threshold_seconds_ = threshold_seconds;
}

void FakeAudioRenderer::SetReferenceClock(::zx::handle ref_clock) {
  FXL_NOTIMPLEMENTED();
}

void FakeAudioRenderer::SendPacket(fuchsia::media::AudioPacket packet,
                                   SendPacketCallback callback) {
  if (dump_packets_) {
    std::cerr << "{ " << packet.timestamp << ", " << packet.payload_size
              << ", 0x" << std::hex << std::setw(16) << std::setfill('0')
              << PacketInfo::Hash(
                     mapped_buffer_.PtrFromOffset(packet.payload_offset),
                     packet.payload_size)
              << std::dec << " },\n";
  }

  if (!expected_packets_info_.empty()) {
    if (expected_packets_info_iter_ == expected_packets_info_.end()) {
      FXL_DLOG(ERROR) << "packet supplied after expected packets";
      expected_ = false;
    }

    if (expected_packets_info_iter_->timestamp() != packet.timestamp ||
        expected_packets_info_iter_->size() != packet.payload_size ||
        expected_packets_info_iter_->hash() !=
            PacketInfo::Hash(
                mapped_buffer_.PtrFromOffset(packet.payload_offset),
                packet.payload_size)) {
      FXL_DLOG(ERROR) << "supplied packet doesn't match expected packet info";
      expected_ = false;
    }

    ++expected_packets_info_iter_;
  }

  packet_queue_.push(std::make_pair(packet, std::move(callback)));

  if (packet_queue_.size() == 1) {
    MaybeScheduleRetirement();
  }
}

void FakeAudioRenderer::SendPacketNoReply(fuchsia::media::AudioPacket packet) {
  SendPacket(std::move(packet), []() {});
}

void FakeAudioRenderer::Flush(FlushCallback callback) {
  while (!packet_queue_.empty()) {
    packet_queue_.front().second();
    packet_queue_.pop();
  }

  callback();
}

void FakeAudioRenderer::FlushNoReply() {
  Flush([]() {});
}

void FakeAudioRenderer::Play(int64_t reference_time, int64_t media_time,
                             PlayCallback callback) {
  if (reference_time == fuchsia::media::kNoTimestamp) {
    reference_time = media::Timeline::local_now();
  }

  if (media_time == fuchsia::media::kNoTimestamp) {
    if (restart_media_time_ != fuchsia::media::kNoTimestamp) {
      media_time = restart_media_time_;
    } else if (packet_queue_.empty()) {
      media_time = 0;
    } else {
      media_time = to_ns(packet_queue_.front().first.timestamp);
    }
  }

  callback(reference_time, media_time);

  timeline_function_ =
      media::TimelineFunction(media_time, reference_time, 1, 1);

  MaybeScheduleRetirement();
}

void FakeAudioRenderer::PlayNoReply(int64_t reference_time,
                                    int64_t media_time) {
  Play(reference_time, media_time,
       [](int64_t reference_time, int64_t media_time) {});
}

void FakeAudioRenderer::Pause(PauseCallback callback) {
  int64_t reference_time = media::Timeline::local_now();
  int64_t media_time = timeline_function_(reference_time);
  timeline_function_ =
      media::TimelineFunction(media_time, reference_time, 0, 1);
  callback(reference_time, media_time);
}

void FakeAudioRenderer::PauseNoReply() {
  Pause([](int64_t reference_time, int64_t media_time) {});
}

void FakeAudioRenderer::SetGainMute(float gain, bool mute, uint32_t flags,
                                    SetGainMuteCallback callback) {
  gain_ = gain;
  mute_ = mute;
  gain_mute_flags_ = flags;
  callback(gain, mute);
}

void FakeAudioRenderer::SetGainMuteNoReply(float gain, bool mute,
                                           uint32_t flags) {
  SetGainMute(gain, mute, flags, [](float gain, bool mute) {});
}

void FakeAudioRenderer::DuplicateGainControlInterface(
    ::fidl::InterfaceRequest<fuchsia::media::AudioRendererGainControl>
        request) {
  FXL_NOTIMPLEMENTED();
}

void FakeAudioRenderer::EnableMinLeadTimeEvents(bool enabled) {
  if (enabled) {
    binding_.events().OnMinLeadTimeChanged(min_lead_time_ns_);
  }
}

void FakeAudioRenderer::GetMinLeadTime(GetMinLeadTimeCallback callback) {
  callback(min_lead_time_ns_);
}

void FakeAudioRenderer::MaybeScheduleRetirement() {
  if (!progressing() || packet_queue_.empty()) {
    return;
  }

  int64_t reference_time = timeline_function_.ApplyInverse(
      to_ns(packet_queue_.front().first.timestamp));

  async::PostTaskForTime(
      async_,
      [this]() {
        if (!progressing() || packet_queue_.empty()) {
          return;
        }

        int64_t reference_time = timeline_function_.ApplyInverse(
            to_ns(packet_queue_.front().first.timestamp));

        if (reference_time <= media::Timeline::local_now()) {
          packet_queue_.front().second();
          packet_queue_.pop();
        }

        MaybeScheduleRetirement();
      },
      zx::time(reference_time));
}

}  // namespace test
}  // namespace media_player
