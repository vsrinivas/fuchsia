// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_RING_BUFFER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_RING_BUFFER_H_

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

namespace media::audio {

class DriverRingBuffer : public fbl::RefCounted<DriverRingBuffer> {
 public:
  static fbl::RefPtr<DriverRingBuffer> Create(zx::vmo vmo, uint32_t frame_size,
                                              uint32_t frame_count, bool input);

  uint64_t size() const { return vmo_mapper_.size(); }
  uint32_t frames() const { return frames_; }
  uint32_t frame_size() const { return frame_size_; }
  uint8_t* virt() const {
    return reinterpret_cast<uint8_t*>(vmo_mapper_.start());
  }

 private:
  friend class fbl::RefPtr<DriverRingBuffer>;

  DriverRingBuffer() {}
  ~DriverRingBuffer() {}

  zx_status_t Init(zx::vmo vmo, uint32_t frame_size, uint32_t frame_count,
                   bool input);

  fzl::VmoMapper vmo_mapper_;
  uint32_t frames_ = 0;
  uint32_t frame_size_ = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_RING_BUFFER_H_
