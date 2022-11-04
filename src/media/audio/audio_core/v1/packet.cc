// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/packet.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace media::audio {

Packet::Packet(fbl::RefPtr<RefCountedVmoMapper> vmo_ref, size_t vmo_offset_bytes, int64_t length,
               Fixed start, async_dispatcher_t* callback_dispatcher, fit::closure callback)
    : vmo_ref_(std::move(vmo_ref)),
      vmo_offset_bytes_(vmo_offset_bytes),
      length_(length),
      start_(start),
      dispatcher_(callback_dispatcher),
      callback_(std::move(callback)) {
  TRACE_DURATION("audio", "Packet::Packet");
  TRACE_FLOW_BEGIN("audio.debug", "process_packet", nonce_);
  FX_DCHECK(vmo_ref_ != nullptr);
}

Packet::~Packet() {
  TRACE_DURATION("audio", "Packet::~Packet");
  TRACE_FLOW_END("audio.debug", "process_packet", nonce_);
  if (callback_) {
    async::PostTask(dispatcher_, std::move(callback_));
  }
}

}  // namespace media::audio
