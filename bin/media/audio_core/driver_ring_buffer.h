// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_DRIVER_RING_BUFFER_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_DRIVER_RING_BUFFER_H_

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/zx/vmo.h>

namespace media {
namespace audio {

class DriverRingBuffer : public fbl::RefCounted<DriverRingBuffer> {
 public:
  static fbl::RefPtr<DriverRingBuffer> Create(zx::vmo vmo, uint32_t frame_size,
                                              uint32_t frame_count, bool input);

  uint64_t size() const { return size_; }
  uint32_t frames() const { return frames_; }
  uint32_t frame_size() const { return frame_size_; }
  uint8_t* virt() const { return reinterpret_cast<uint8_t*>(virt_); }

 private:
  friend class fbl::RefPtr<DriverRingBuffer>;

  DriverRingBuffer() {}
  ~DriverRingBuffer();

  zx_status_t Init(zx::vmo vmo, uint32_t frame_size, uint32_t frame_count,
                   bool input);

  zx::vmo vmo_;
  uint64_t size_ = 0;
  uint32_t frames_ = 0;
  uint32_t frame_size_ = 0;
  void* virt_ = nullptr;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_DRIVER_RING_BUFFER_H_
