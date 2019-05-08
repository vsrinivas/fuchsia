// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <utility>

// TODO(ZX-4003): Required for SpaceManager interface. Can we shrink this dependency?
#include <blobfs/allocator.h>
#include <lib/fzl/owned-vmo-mapper.h>

namespace blobfs {

// Block-aligned VMO-backed buffer registered with the underlying device.
//
// This class is movable but not copyable.
// This class is thread-compatible.
class VmoBuffer {
public:
    VmoBuffer() = default;
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
    zx_status_t Initialize(SpaceManager* space_manager, size_t blocks, const char* label);

    // Returns the total amount of pending blocks which may be buffered.
    size_t capacity() const { return capacity_; }

    // Returns the vmoid of the underlying VmoBuffer.
    vmoid_t vmoid() const { return vmoid_; }

    // Returns a const view of the underlying VMO.
    const zx::vmo& vmo() const { return mapper_.vmo(); }

    // Returns data starting at block |index| in the buffer.
    void* MutableData(size_t index);

private:
    void Reset();

    SpaceManager* space_manager_ = nullptr;
    fzl::OwnedVmoMapper mapper_;
    vmoid_t vmoid_ = VMOID_INVALID;
    size_t capacity_ = 0;
};

} // namespace blobfs
