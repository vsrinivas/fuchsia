// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_VMO_BUFFER_H_
#define BLOBFS_VMO_BUFFER_H_

#include <lib/fzl/owned-vmo-mapper.h>

#include <utility>

#include <blobfs/block-buffer.h>
#include <blobfs/vmoid-registry.h>

namespace blobfs {

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
  VmoBuffer(VmoidRegistry* registry, fzl::OwnedVmoMapper mapper, vmoid_t vmoid, size_t capacity)
      : vmoid_registry_(registry), mapper_(std::move(mapper)), vmoid_(vmoid), capacity_(capacity) {}
  VmoBuffer(const VmoBuffer&) = delete;
  VmoBuffer& operator=(const VmoBuffer&) = delete;
  VmoBuffer(VmoBuffer&& other);
  VmoBuffer& operator=(VmoBuffer&& other);
  ~VmoBuffer();

  // Initializes the buffer VMO with |blocks| blocks of size kBlobfsBlockSize.
  //
  // Returns an error if the VMO cannot be created, mapped, or attached to the
  // underlying storage device.
  //
  // Should only be called on VmoBuffers which have not been initialized already.
  zx_status_t Initialize(VmoidRegistry* vmoid_registry, size_t blocks, const char* label);

  // Returns a const view of the underlying VMO.
  const zx::vmo& vmo() const { return mapper_.vmo(); }

  // BlockBuffer interface:

  size_t capacity() const final { return capacity_; }

  vmoid_t vmoid() const final { return vmoid_; }

  void* Data(size_t index) final;

  const void* Data(size_t index) const final;

 private:
  void Reset();

  VmoidRegistry* vmoid_registry_ = nullptr;
  fzl::OwnedVmoMapper mapper_;
  vmoid_t vmoid_ = VMOID_INVALID;
  size_t capacity_ = 0;
};

}  // namespace blobfs

#endif  // BLOBFS_VMO_BUFFER_H_
