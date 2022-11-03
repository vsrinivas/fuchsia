// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_VMO_POOL_WRAPPER_VMO_POOL_WRAPPER_H_
#define SRC_CAMERA_LIB_VMO_POOL_WRAPPER_VMO_POOL_WRAPPER_H_

#include <lib/fzl/vmo-pool.h>

#include <optional>
#include <string>

namespace camera {

// Wrapper class for fzl::VmoPool. Its functionality and API is equivalent to VmoPool, but
// adds syslogs relevant to Camera.
class VmoPoolWrapper {
 public:
  VmoPoolWrapper() = default;
  virtual ~VmoPoolWrapper() = default;

  // Initializes the VmoPool with a set of vmos.
  zx_status_t Init(cpp20::span<zx::unowned_vmo> vmos,
                   std::optional<std::string> name = std::nullopt);

  // Pin all the vmos to physical memory.  This must be called prior to
  // requesting a physical address from any Buffer instance.
  zx_status_t PinVmos(const zx::bti& bti, fzl::VmoPool::RequireContig req_contiguous,
                      fzl::VmoPool::RequireLowMem req_low_memory) {
    return pool_.PinVmos(bti, req_contiguous, req_low_memory);
  }

  // Map the vmos to virtual memory. This must be called prior to
  // requesting a virtual address from any Buffer instance.
  zx_status_t MapVmos() { return pool_.MapVmos(); }

  // Resets the buffer read and write locks.
  void Reset() { return pool_.Reset(); }

  // Finds the next available buffer, locks that buffer for writing, and
  // returns a Buffer instance to allow access to that buffer.
  // If no buffers are available, returns an empty std::optional.
  std::optional<fzl::VmoPool::Buffer> LockBufferForWrite();

  // Unlocks the buffer with the specified index and sets it as ready to be
  // reused.
  zx_status_t ReleaseBuffer(uint32_t buffer_index) { return pool_.ReleaseBuffer(buffer_index); }

  // Returns the total number of buffers in this pool.
  size_t total_buffers() const { return pool_.total_buffers(); }

  // Returns the number of free buffers in this pool.
  size_t free_buffers() const { return pool_.free_buffers(); }

  // Returns the size (in bytes) of the buffer at a given index in this pool.
  size_t buffer_size(uint32_t buffer_index = 0) const { return pool_.buffer_size(buffer_index); }

 private:
  std::string CreateLogString(const std::string& message);

  fzl::VmoPool pool_;

  // Tracks the minimum number of free buffers as they are locked for write.
  uint32_t min_free_buffers_ = 0;
  std::string name_;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_VMO_POOL_WRAPPER_VMO_POOL_WRAPPER_H_
