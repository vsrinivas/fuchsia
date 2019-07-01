// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_packet_ref.h"

#include "src/lib/fxl/logging.h"

namespace media::audio {

AudioPacketRef::AudioPacketRef(fbl::RefPtr<RefCountedVmoMapper> vmo_ref,
                               fuchsia::media::AudioRenderer::SendPacketCallback callback,
                               fuchsia::media::StreamPacket packet, ReleaseHandler release_handler,
                               uint32_t frac_frame_len, int64_t start_pts)
    : vmo_ref_(std::move(vmo_ref)),
      callback_(std::move(callback)),
      packet_(packet),
      frac_frame_len_(frac_frame_len),
      start_pts_(start_pts),
      end_pts_(start_pts + frac_frame_len),
      release_handler_(std::move(release_handler)) {
  FXL_DCHECK(release_handler_);
  FXL_DCHECK(vmo_ref_ != nullptr);
}

void AudioPacketRef::fbl_recycle() {
  // If the packet is dying for the first time, and we successfully queue it for
  // cleanup, allow it to live on until the cleanup actually runs.  Otherwise
  // the object is at its end of life.
  if (!was_recycled_) {
    was_recycled_ = true;
    if (NeedsCleanup()) {
      FXL_DCHECK(release_handler_);
      release_handler_(std::unique_ptr<AudioPacketRef>(this));
      return;
    }
  }

  delete this;
}

}  // namespace media::audio
