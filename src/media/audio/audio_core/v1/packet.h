// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PACKET_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PACKET_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <stdint.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>

#include "src/media/audio/audio_core/v1/utils.h"
#include "src/media/audio/lib/format/constants.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio {

class Packet;

namespace internal {
using PacketAllocatorTraits =
    ::fbl::SlabAllocatorTraits<fbl::RefPtr<Packet>, fbl::DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE,
                               fbl::Mutex, fbl::SlabAllocatorFlavor::INSTANCED,
                               fbl::SlabAllocatorOptions::EnableObjectCount>;
}

class Packet : public fbl::SlabAllocated<internal::PacketAllocatorTraits>,
               public fbl::RefCounted<Packet> {
 public:
  using AllocatorTraits = internal::PacketAllocatorTraits;
  using Allocator = ::fbl::SlabAllocator<AllocatorTraits>;

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
  Fixed start() const { return start_; }
  Fixed end() const { return start_ + Fixed(length_); }
  int64_t length() const { return length_; }

  void* payload() { return reinterpret_cast<uint8_t*>(vmo_ref_->start()) + vmo_offset_bytes_; }

  void Display() {
    FX_LOGS(INFO) << ffl::String::DecRational << "Packet start " << start_ << ", length "
                  << length_;
  }

 protected:
  friend Allocator;

  // fbl::SlabAllocated _requires_ instances to be sourced from an fbl::SlabAllocator. Make this
  // ctor non-public to prevent other ways of instantiation.
  Packet(fbl::RefPtr<RefCountedVmoMapper> vmo_ref, size_t vmo_offset_bytes, int64_t frame_count,
         Fixed start_frame, async_dispatcher_t* callback_dispatcher, fit::closure callback);

 private:
  fbl::RefPtr<RefCountedVmoMapper> vmo_ref_;
  size_t vmo_offset_bytes_;

  int64_t length_;
  Fixed start_;

  async_dispatcher_t* dispatcher_;
  fit::closure callback_;
  trace_async_id_t nonce_ = TRACE_NONCE();
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PACKET_H_
