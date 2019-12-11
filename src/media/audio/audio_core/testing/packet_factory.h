// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_PACKET_FACTORY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_PACKET_FACTORY_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/format.h"
#include "src/media/audio/audio_core/packet.h"
#include "src/media/audio/audio_core/utils.h"

namespace media::audio::testing {

// Helper class for creating packets for testing.
class PacketFactory {
 public:
  PacketFactory(async_dispatcher_t* dispatcher, const Format& format, size_t vmo_size);

  fbl::RefPtr<Packet> CreatePacket(float sample, zx::duration duration = zx::msec(1),
                                   fit::closure callback = nullptr);

  const Format& format() const { return format_; }

 private:
  async_dispatcher_t* dispatcher_;
  Format format_;
  fbl::RefPtr<RefCountedVmoMapper> vmo_ref_;
  size_t buffer_offset_ = 0;
  FractionalFrames<int64_t> next_pts_{0};
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_PACKET_FACTORY_H_
