// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_PACKET_REF_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_PACKET_REF_H_

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/vmo-utils/vmo_mapper.h>
#include <stdint.h>

#include "lib/fxl/logging.h"

namespace media {
namespace audio {

class AudioCoreImpl;

// TODO(johngro): Consider moving instances of this class to a slab allocation
// pattern.  They are the most frequently allocated object in the mixer (easily
// 100s per second) and they do not live very long at all (300-400mSec at most),
// so they could easily be causing heap fragmentation issues.
class AudioPacketRef
    : public fbl::RefCounted<AudioPacketRef>,
      public fbl::Recyclable<AudioPacketRef>,
      public fbl::DoublyLinkedListable<fbl::unique_ptr<AudioPacketRef>> {
 public:
  AudioPacketRef(fbl::RefPtr<vmo_utils::RefCountedVmoMapper> vmo_ref,
                 fuchsia::media::AudioOut::SendPacketCallback callback,
                 fuchsia::media::StreamPacket packet, AudioCoreImpl* server,
                 uint32_t frac_frame_len, int64_t start_pts);

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
  // simply in media time instead of converting to fractional units of
  // renderer frames.  If/when outputs move away from a single fixed step size
  // for output sampling, it will probably be best to just convert this back
  // to media time.
  int64_t start_pts() const { return start_pts_; }
  int64_t end_pts() const { return end_pts_; }
  uint32_t frac_frame_len() const { return frac_frame_len_; }

  void Cleanup() {
    FXL_DCHECK(callback_ != nullptr);
    callback_();
  }
  void* payload() {
    auto start = reinterpret_cast<uint8_t*>(vmo_ref_->start());
    return (start + packet_.payload_offset);
  }
  uint32_t flags() { return packet_.flags; }

 protected:
  friend class fbl::RefPtr<AudioPacketRef>;
  friend class fbl::Recyclable<AudioPacketRef>;
  friend class fbl::unique_ptr<AudioPacketRef>;

  ~AudioPacketRef() = default;

  // Check to see if this packet has a valid callback.  If so, when it gets
  // recycled for the first time, it needs to be kept alive and posted to the
  // service's cleanup queue so that the user's callback gets called on the main
  // service dispatcher thread.
  bool NeedsCleanup() { return callback_ != nullptr; }

  fbl::RefPtr<vmo_utils::RefCountedVmoMapper> vmo_ref_;
  fuchsia::media::AudioOut::SendPacketCallback callback_;
  fuchsia::media::StreamPacket packet_;

  AudioCoreImpl* const service_;
  uint32_t frac_frame_len_;
  int64_t start_pts_;
  int64_t end_pts_;
  bool was_recycled_ = false;

 private:
  void fbl_recycle();
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_PACKET_REF_H_
