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

#include "src/media/audio/audio_core/mixer/frames.h"
#include "src/media/audio/audio_core/utils.h"

namespace media::audio {

// TODO(johngro): Consider moving instances of this class to a slab allocation
// pattern.  They are the most frequently allocated object in the mixer (easily
// 100s per second) and they do not live very long at all (300-400mSec at most),
// so they could easily be causing heap fragmentation issues.
class Packet : public fbl::RefCounted<Packet> {
 public:
  Packet(fbl::RefPtr<RefCountedVmoMapper> vmo_ref, size_t vmo_offset_bytes,
         FractionalFrames<uint32_t> frac_frame_len, FractionalFrames<int64_t> start_frame,
         async_dispatcher_t* callback_dispatcher, fit::closure callback);

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
  // Note, the |start| is the time (expressed in fractional frames, on the
  // source's timeline) at which the first frame of audio in the packet should
  // be presented.  The |end| is the time at which the frame after the final
  // frame in the packet would be presented.
  FractionalFrames<int64_t> start() const { return start_; }
  FractionalFrames<int64_t> end() const { return start_ + length_; }
  FractionalFrames<uint32_t> length() const { return length_; }

  void* payload() { return reinterpret_cast<uint8_t*>(vmo_ref_->start()) + vmo_offset_bytes_; }

 private:
  fbl::RefPtr<RefCountedVmoMapper> vmo_ref_;
  size_t vmo_offset_bytes_;

  FractionalFrames<uint32_t> length_;
  FractionalFrames<int64_t> start_;

  async_dispatcher_t* dispatcher_;
  fit::closure callback_;
  trace_async_id_t nonce_ = TRACE_NONCE();
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PACKET_H_
