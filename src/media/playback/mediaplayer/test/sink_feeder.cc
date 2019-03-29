// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/sink_feeder.h"

#include "src/lib/fxl/logging.h"

namespace media_player {
namespace test {

static const uint32_t kPayloadBufferId = 0;

zx_status_t SinkFeeder::Init(fuchsia::media::SimpleStreamSinkPtr sink,
                             size_t size, uint32_t frame_size,
                             uint32_t max_packet_size,
                             uint32_t max_packet_count) {
  FXL_DCHECK(sink);
  FXL_DCHECK(size > 0);
  FXL_DCHECK(frame_size > 0);
  FXL_DCHECK(max_packet_size > 0);
  FXL_DCHECK(max_packet_count > 0);

  sink_ = std::move(sink);
  bytes_remaining_ = size;
  frame_size_ = frame_size;
  max_packet_size_ = max_packet_size;

  // Create a VMO in which to share packet payloads. We only really need this
  // VMO to be |max_packet_size * max_packet_count| bytes, but we make it |size|
  // bytes just to make payload allocation simpler. Production code should
  // minimize the size of the VMO(s) and keep track of free regions.
  zx::vmo vmo;
  zx_status_t status = vmo_mapper_.CreateAndMap(
      size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo,
      ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE);

  if (status != ZX_OK) {
    return status;
  }

  // Fill the VMO with a terrible noise.
  uint8_t* p = reinterpret_cast<uint8_t*>(vmo_mapper_.start());
  for (size_t i = 0; i < size; ++i) {
    *p = static_cast<uint8_t>(i ^ (i >> 8));
    ++p;
  }

  // Register the VMO with the sink.
  sink_->AddPayloadBuffer(kPayloadBufferId, std::move(vmo));

  // Send |max_packet_count| packets right away. We will endeavor to keep this
  // many packets pending at the sink until we run out of packets.
  for (uint32_t i = 0; i < max_packet_count; ++i) {
    MaybeSendPacket();
  }

  return ZX_OK;
}

void SinkFeeder::MaybeSendPacket() {
  if (bytes_remaining_ == 0) {
    // We've sent all the packets.

    if (!end_of_stream_sent_) {
      // We haven't told the sink that the stream has ended yet. Do so now.
      sink_->EndOfStream();
      end_of_stream_sent_ = true;
    }

    return;
  }

  // Prepare the next packet.
  uint32_t packet_size = std::min(bytes_remaining_, max_packet_size_);
  fuchsia::media::StreamPacket packet;
  packet.pts = position_ / frame_size_;
  packet.payload_buffer_id = kPayloadBufferId;
  packet.payload_offset = position_;
  packet.payload_size = packet_size;

  bytes_remaining_ -= packet_size;

  position_ += packet_size;

  // Send the packet.
  sink_->SendPacket(std::move(packet), [this]() {
    // The sink is done with the packet. Send another if we're not done.
    MaybeSendPacket();
  });
}

}  // namespace test
}  // namespace media_player
