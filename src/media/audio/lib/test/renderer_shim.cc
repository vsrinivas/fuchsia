// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/renderer_shim.h"

#include "src/media/audio/lib/logging/logging.h"

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

void RendererShimImpl::Play(TestFixture* fixture, int64_t reference_time, int64_t media_time) {
  bool played = false;
  renderer_->Play(reference_time, media_time,
                  [&played](int64_t reference_time, int64_t media_time) { played = true; });

  fixture->RunLoopUntil([&played]() { return played; });
  ASSERT_FALSE(fixture->error_occurred());
}

void RendererShimImpl::SendPackets(size_t num_packets, int64_t initial_pts) {
  FX_CHECK(num_packets <= num_payload_packets());

  for (auto packet_num = 0u; packet_num < num_packets; ++packet_num) {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = packet_num * num_packet_bytes();
    packet.payload_size = num_packet_bytes();
    packet.pts = initial_pts + (packet_num * num_packet_frames());

    AUDIO_LOG(DEBUG) << " sending pkt " << packet_num << " " << packet.pts;
    renderer_->SendPacket(packet, [this, packet_num]() {
      AUDIO_LOG(DEBUG) << " return: pkt " << packet_num;
      received_packet_num_ = packet_num;
    });
  }
}

void RendererShimImpl::WaitForPacket(TestFixture* fixture, size_t packet_num) {
  received_packet_num_ = -1;
  fixture->RunLoopUntil([this, packet_num]() {
    return received_packet_num_ >= 0 && (received_packet_num_ >= static_cast<ssize_t>(packet_num));
  });
  ASSERT_FALSE(fixture->error_occurred());
}

}  // namespace media::audio::test
