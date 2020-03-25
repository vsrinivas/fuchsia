// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_BUFFER_VMO_BUFFER_H_
#define STORAGE_BUFFER_VMO_BUFFER_H_

#include <lib/fzl/owned-vmo-mapper.h>

#include <storage/buffer/block_buffer.h>
#include <storage/buffer/vmoid_registry.h>

namespace storage {

// Block-aligned VMO-backed buffer registered with the underlying device.
//
// This class is movable but not copyable.
// This class is thread-compatible.
class VmoBuffer final : public BlockBuffer {
 public:
  VmoBuffer() = default;
  // Constructor for a pre-registered VMO.
  //
  // Prefer using |VmoBuffer.Initialize|.
  VmoBuffer(VmoidRegistry* registry, fzl::OwnedVmoMapper mapper, vmoid_t vmoid, size_t capacity,
            uint32_t block_size)
      : vmoid_registry_(registry),
        mapper_(std::move(mapper)),
        vmoid_(vmoid),
        block_size_(block_size),
        capacity_(capacity) {}

  VmoBuffer(const VmoBuffer&) = delete;
  VmoBuffer& operator=(const VmoBuffer&) = delete;
  VmoBuffer(VmoBuffer&& other);
  VmoBuffer& operator=(VmoBuffer&& other);
  ~VmoBuffer();

  // Initializes the buffer VMO with |blocks| blocks of size |block_size|.
  //
  // Returns an error if the VMO cannot be created, mapped, or attached to the
  // underlying storage device.
  //
  // Should only be called on VmoBuffers which have not been initialized already.
  zx_status_t Initialize(VmoidRegistry* vmoid_registry, size_t blocks, uint32_t block_size,
                         const char* label);

  // Returns a const view of the underlying VMO.
  const zx::vmo& vmo() const { return mapper_.vmo(); }

  // BlockBuffer interface:

  size_t capacity() const final { return capacity_; }

  uint32_t BlockSize() const final { return block_size_; }

  vmoid_t vmoid() const final { return vmoid_; }

  void* Data(size_t index) final;

  const void* Data(size_t index) const final;

 private:
  void Reset();

  VmoidRegistry* vmoid_registry_ = nullptr;
  fzl::OwnedVmoMapper mapper_;
  vmoid_t vmoid_ = BLOCK_VMOID_INVALID;
  uint32_t block_size_ = 0;
  size_t capacity_ = 0;
};

}  // namespace storage

#endif  // STORAGE_BUFFER_VMO_BUFFER_H_
