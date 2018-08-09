// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_BUFFER_H_
#define GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_BUFFER_H_

#include <stdint.h>

#include <memory>

#include <lib/fzl/vmo-mapper.h>
#include <zircon/status.h>
#include <zx/event.h>
#include <zx/vmo.h>

namespace simple_camera {

// Encapsulates a part of a VMO.
// A Buffer represents one frame, mapping it into memory to allow the process to
// write into it. Buffer also keeps track of the locked state of the memory.
class Buffer : public fzl::VmoMapper {
 protected:
  enum class BufferState { kInvalid = 0, kAvailable, kReadLocked };

 public:
  virtual ~Buffer() {}

  // Unlock the memory block
  virtual void Reset() { state_ = BufferState::kAvailable; }

  // Lock the memory block
  virtual void Signal() { state_ = BufferState::kReadLocked; }

  bool IsAvailable() { return state_ == BufferState::kAvailable; }

  // Assumes that the buffer is set up as an ARGB image,
  // with 4 bytes per pixel.  Fills the entire size of the buffer
  // with a set color with the red, green and blue channels
  // indicated by the r, g and b arguments.
  void FillARGB(uint8_t r, uint8_t g, uint8_t b);

  // Create a duplicate of this buffer's VMO handle.
  zx_status_t DuplicateVmoWithoutWrite(zx::vmo* result);

  // Create a new buffer, with size |buffer_size| and an offset of |offset|
  // in the vmo |main_buffer|.  Will return a nullptr if mapping or allocating
  // fails.
  static std::unique_ptr<Buffer> Create(uint64_t buffer_size,
                                        const zx::vmo& main_buffer,
                                        uint64_t offset);

  // Writes the contents of the buffer to a file, no header.
  zx_status_t SaveToFile(const char* filename);

  uint64_t vmo_offset() const { return vmo_offset_; }

 protected:
  // For use during creation only:
  zx_status_t DuplicateAndMapVmo(uint64_t buffer_size,
                                 const zx::vmo& main_buffer, uint64_t offset);

  Buffer() = default;

  zx::vmo vmo_;
  uint64_t vmo_offset_;
  BufferState state_ = BufferState::kInvalid;
};

}  // namespace simple_camera

#endif  // GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_BUFFER_H_
