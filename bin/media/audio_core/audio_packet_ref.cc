// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/audio_packet_ref.h"

#include "garnet/bin/media/audio_core/audio_core_impl.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {

AudioPacketRef::AudioPacketRef(
    fbl::RefPtr<vmo_utils::RefCountedVmoMapper> vmo_ref,
    fuchsia::media::AudioOut::SendPacketCallback callback,
    fuchsia::media::StreamPacket packet, AudioCoreImpl* service,
    uint32_t frac_frame_len, int64_t start_pts)
    : vmo_ref_(std::move(vmo_ref)),
      callback_(std::move(callback)),
      packet_(std::move(packet)),
      service_(service),
      frac_frame_len_(frac_frame_len),
      start_pts_(start_pts),
      end_pts_(start_pts + frac_frame_len) {
  FXL_DCHECK(service_);
  FXL_DCHECK(vmo_ref_ != nullptr);
}

void AudioPacketRef::fbl_recycle() {
  // If the packet is dying for the first time, and we successfully queue it for
  // cleanup, allow it to live on until the cleanup actually runs.  Otherwise
  // the object is at its end of life.
  if (!was_recycled_) {
    was_recycled_ = true;
    if (NeedsCleanup()) {
      FXL_DCHECK(service_);
      service_->SchedulePacketCleanup(fbl::unique_ptr<AudioPacketRef>(this));
      return;
    }
  }

  delete this;
}

}  // namespace audio
}  // namespace media
