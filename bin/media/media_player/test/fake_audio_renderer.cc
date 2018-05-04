// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/test/fake_audio_renderer.h"

#include <iomanip>
#include <iostream>
#include <limits>

#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"

namespace media_player {

namespace {

static uint64_t Hash(const void* data, size_t data_size) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  uint64_t hash = 0;

  for (; data_size != 0; --data_size, ++bytes) {
    hash = *bytes + (hash << 6) + (hash << 16) - hash;
  }

  return hash;
}

}  // namespace

FakeAudioRenderer::FakeAudioRenderer() : binding_(this) {}

FakeAudioRenderer::~FakeAudioRenderer() {}

void FakeAudioRenderer::Bind(
    fidl::InterfaceRequest<media::AudioRenderer2> renderer) {
  binding_.Bind(std::move(renderer));
}

void FakeAudioRenderer::SetPcmFormat(media::AudioPcmFormat format) {
  format_ = format;
}

void FakeAudioRenderer::SetPayloadBuffer(::zx::vmo payload_buffer) {
  mapped_buffer_.InitFromVmo(std::move(payload_buffer), ZX_VM_FLAG_PERM_READ);
}

void FakeAudioRenderer::SetPtsUnits(uint32_t tick_per_second_numerator,
                                    uint32_t tick_per_second_denominator) {
  tick_per_second_numerator_ = tick_per_second_numerator;
  tick_per_second_denominator_ = tick_per_second_denominator;
}

void FakeAudioRenderer::SetPtsContinuityThreshold(float threshold_seconds) {
  threshold_seconds_ = threshold_seconds;
}

void FakeAudioRenderer::SetReferenceClock(::zx::handle ref_clock) {
  FXL_NOTIMPLEMENTED();
}

void FakeAudioRenderer::SendPacket(media::AudioPacket packet,
                                   SendPacketCallback callback) {
  if (dump_packets_) {
    std::cerr << "{ " << packet.timestamp << ", " << packet.payload_size
              << ", 0x" << std::hex << std::setw(16) << std::setfill('0')
              << Hash(mapped_buffer_.PtrFromOffset(packet.payload_offset),
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
            Hash(mapped_buffer_.PtrFromOffset(packet.payload_offset),
                 packet.payload_size)) {
      FXL_DLOG(ERROR) << "supplied packet doesn't match expected packet info";
      expected_ = false;
    }

    ++expected_packets_info_iter_;
  }

  if (playing_) {
    callback();
  } else {
    packet_callback_queue_.push(callback);
  }
}

void FakeAudioRenderer::SendPacketNoReply(media::AudioPacket packet) {
  SendPacket(std::move(packet), []() {});
}

void FakeAudioRenderer::Flush(FlushCallback callback) {
  while (!packet_callback_queue_.empty()) {
    packet_callback_queue_.front()();
    packet_callback_queue_.pop();
  }

  callback();
}

void FakeAudioRenderer::FlushNoReply() {
  Flush([]() {});
}

void FakeAudioRenderer::Play(int64_t reference_time,
                             int64_t media_time,
                             PlayCallback callback) {
  playing_ = true;
  callback(0, 0);

  while (!packet_callback_queue_.empty()) {
    packet_callback_queue_.front()();
    packet_callback_queue_.pop();
  }
}

void FakeAudioRenderer::PlayNoReply(int64_t reference_time,
                                    int64_t media_time) {
  Play(reference_time, media_time,
       [](int64_t reference_time, int64_t media_time) {});
}

void FakeAudioRenderer::Pause(PauseCallback callback) {
  playing_ = false;
  callback(0, 0);
}

void FakeAudioRenderer::PauseNoReply() {
  Pause([](int64_t reference_time, int64_t media_time) {});
}

void FakeAudioRenderer::SetGainMute(float gain,
                                    bool mute,
                                    uint32_t flags,
                                    SetGainMuteCallback callback) {
  gain_ = gain;
  mute_ = mute;
  gain_mute_flags_ = flags;
  callback(gain, mute);
}

void FakeAudioRenderer::SetGainMuteNoReply(float gain,
                                           bool mute,
                                           uint32_t flags) {
  SetGainMute(gain, mute, flags, [](float gain, bool mute) {});
}

void FakeAudioRenderer::DuplicateGainControlInterface(
    ::fidl::InterfaceRequest<media::AudioRendererGainControl> request) {
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

}  // namespace media_player
