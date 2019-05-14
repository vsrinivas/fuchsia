// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <blobfs/allocator.h>
#include <fbl/vector.h>

namespace blobfs {

// A trivial space manager, incapable of resizing.
class MockSpaceManager : public SpaceManager {
public:
    Superblock& MutableInfo() {
        return superblock_;
    }

    const Superblock& Info() const final {
        return superblock_;
    }
    zx_status_t AddInodes(fzl::ResizeableVmoMapper* node_map) final {
        return ZX_ERR_NOT_SUPPORTED;
    }
    zx_status_t AddBlocks(size_t nblocks, RawBitmap* map) final {
        return ZX_ERR_NOT_SUPPORTED;
    }
    zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final {
        return ZX_ERR_NOT_SUPPORTED;
    }
    zx_status_t DetachVmo(vmoid_t vmoid) final {
        return ZX_ERR_NOT_SUPPORTED;
    }

private:
    Superblock superblock_{};
};

// Create a block and node map of the requested size, update the superblock of
// the |space_manager|, and create an allocator from this provided info.
void InitializeAllocator(size_t blocks, size_t nodes, MockSpaceManager* space_manager,
                         fbl::unique_ptr<Allocator>* out);

// Force the allocator to become maximally fragmented by allocating
// every-other block within up to |blocks|.
void ForceFragmentation(Allocator* allocator, size_t blocks);

// Save the extents within |in| in a non-reserved vector |out|.
void CopyExtents(const fbl::Vector<ReservedExtent>& in, fbl::Vector<Extent>* out);

// Save the nodes within |in| in a non-reserved vector |out|.
void CopyNodes(const fbl::Vector<ReservedNode>& in, fbl::Vector<uint32_t>* out);

} // namespace blobfs
