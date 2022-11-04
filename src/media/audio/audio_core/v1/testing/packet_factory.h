// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_PACKET_FACTORY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_PACKET_FACTORY_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/v1/packet.h"
#include "src/media/audio/audio_core/v1/utils.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio::testing {

// Helper class for creating packets for testing.
class PacketFactory {
 public:
  PacketFactory(async_dispatcher_t* dispatcher, const Format& format, size_t vmo_size);

  fbl::RefPtr<Packet> CreatePacket(float val, zx::duration duration = zx::msec(1),
                                   fit::closure callback = nullptr);

  const Format& format() const { return format_; }
  void SeekToFrame(Fixed frame_num) { next_pts_ = frame_num; }

 private:
  Packet::Allocator allocator_{1, true};
  async_dispatcher_t* dispatcher_;
  Format format_;
  fbl::RefPtr<RefCountedVmoMapper> vmo_ref_;
  size_t buffer_offset_ = 0;
  Fixed next_pts_{0};
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_PACKET_FACTORY_H_
