// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/testing/packet_factory.h"

#include <lib/async/cpp/time.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/audio_core/v1/mixer/constants.h"

namespace media::audio::testing {

PacketFactory::PacketFactory(async_dispatcher_t* dispatcher, const Format& format, size_t vmo_size)
    : dispatcher_(dispatcher),
      format_(format),
      vmo_ref_(fbl::MakeRefCounted<RefCountedVmoMapper>()) {
  zx_status_t status = vmo_ref_->CreateAndMap(vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  FX_CHECK(status == ZX_OK);
}

fbl::RefPtr<Packet> PacketFactory::CreatePacket(float val, zx::duration duration,
                                                fit::closure callback) {
  int64_t frame_count = format().frames_per_ns().Scale(duration.to_nsecs());
  size_t payload_offset = buffer_offset_;
  size_t payload_size = format().bytes_per_frame() * frame_count;
  buffer_offset_ += payload_size;

  FX_CHECK(payload_offset + payload_size <= vmo_ref_->size())
      << "Payload offset + size cannot exceed " << vmo_ref_->size();

  // Write the data to the packet buffer.
  float* samples =
      reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(vmo_ref_->start()) + payload_offset);
  auto sample_count = frame_count * format().channels();
  for (uint32_t i = 0; i < sample_count; ++i) {
    samples[i] = val;
  }

  auto packet_ref = allocator_.New(vmo_ref_, payload_offset, frame_count, next_pts_, dispatcher_,
                                   std::move(callback));
  next_pts_ = packet_ref->end();
  return packet_ref;
}

}  // namespace media::audio::testing
