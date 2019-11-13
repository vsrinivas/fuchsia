// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/packet.h"

#include <lib/async/cpp/task.h>

#include <trace/event.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {

Packet::Packet(fbl::RefPtr<RefCountedVmoMapper> vmo_ref, async_dispatcher_t* callback_dispatcher,
               fit::closure callback, fuchsia::media::StreamPacket packet, uint32_t frac_frame_len,
               int64_t start_pts)
    : vmo_ref_(std::move(vmo_ref)),
      callback_(std::move(callback)),
      packet_(packet),
      frac_frame_len_(frac_frame_len),
      start_pts_(start_pts),
      end_pts_(start_pts + frac_frame_len),
      dispatcher_(callback_dispatcher) {
  TRACE_DURATION("audio", "Packet::Packet");
  TRACE_FLOW_BEGIN("audio.debug", "process_packet", nonce_);
  FX_DCHECK(dispatcher_ != nullptr);
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
