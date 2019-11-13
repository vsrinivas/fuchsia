// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_PACKET_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_PACKET_H_

#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <trace/event.h>

#include "src/media/audio/audio_core/utils.h"

namespace media::audio {

// TODO(johngro): Consider moving instances of this class to a slab allocation
// pattern.  They are the most frequently allocated object in the mixer (easily
// 100s per second) and they do not live very long at all (300-400mSec at most),
// so they could easily be causing heap fragmentation issues.
class Packet : public fbl::RefCounted<Packet> {
 public:
  Packet(fbl::RefPtr<RefCountedVmoMapper> vmo_ref, async_dispatcher_t* callback_dispatcher,
         fit::closure callback, fuchsia::media::StreamPacket packet, uint32_t frac_frame_len,
         int64_t start_pts);

  ~Packet();

  // Accessors for starting and ending presentation time stamps expressed in
  // units of audio frames (note, not media time), as signed 50.13 fixed point
  // integers (see kPtsFractionalBits).  At 192KHz, this allows for ~186.3
  // years of usable range when starting from a media time of 0.
  //
  // AudioPackets consumed by the AudioCore are all expected to have
  // explicit presentation time stamps.  If packets sent by the user are
  // missing timestamps, appropriate timestamps will be synthesized at this
  // point in the pipeline.
  //
  // Note, the start pts is the time at which the first frame of audio in the
  // packet should be presented.  The end_pts is the time at which the frame
  // after the final frame in the packet would be presented.
  //
  // TODO(johngro): Reconsider this.  It may be best to keep things expressed
  // simply in media time instead of converting to fractional units of renderer
  // frames.  If/when outputs move away from a single fixed step size for output
  // sampling, it will probably be best to just convert this back to media time.
  int64_t start_pts() const { return start_pts_; }
  int64_t end_pts() const { return end_pts_; }
  uint32_t frac_frame_len() const { return frac_frame_len_; }

  void* payload() {
    auto start = reinterpret_cast<uint8_t*>(vmo_ref_->start());
    return (start + packet_.payload_offset);
  }
  uint32_t flags() const { return packet_.flags; }
  uint32_t payload_buffer_id() const { return packet_.payload_buffer_id; }

 private:
  fbl::RefPtr<RefCountedVmoMapper> vmo_ref_;
  fit::closure callback_;
  fuchsia::media::StreamPacket packet_;

  uint32_t frac_frame_len_;
  int64_t start_pts_;
  int64_t end_pts_;

  async_dispatcher_t* dispatcher_;
  trace_async_id_t nonce_ = TRACE_NONCE();
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PACKET_H_
