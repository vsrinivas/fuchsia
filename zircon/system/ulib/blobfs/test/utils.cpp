// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <zxtest/zxtest.h>

using id_allocator::IdAllocator;

namespace blobfs {

// Create a block and node map of the requested size, update the superblock of
// the |space_manager|, and create an allocator from this provided info.
void InitializeAllocator(size_t blocks, size_t nodes, MockSpaceManager* space_manager,
                         fbl::unique_ptr<Allocator>* out) {
    RawBitmap block_map;
    ASSERT_EQ(ZX_OK, block_map.Reset(blocks));
    fzl::ResizeableVmoMapper node_map;
    ASSERT_EQ(ZX_OK, node_map.CreateAndMap(nodes * kBlobfsBlockSize, "node map"));

    space_manager->MutableInfo().inode_count = nodes;
    space_manager->MutableInfo().data_block_count = blocks;
    std::unique_ptr<IdAllocator> nodes_bitmap = {};
    ASSERT_EQ(ZX_OK, IdAllocator::Create(nodes, &nodes_bitmap), "nodes bitmap");
    *out = std::make_unique<Allocator>(space_manager, std::move(block_map), std::move(node_map),
                                       std::move(nodes_bitmap));
    (*out)->SetLogging(false);
}

// Force the allocator to become maximally fragmented by allocating
// every-other block within up to |blocks|.
void ForceFragmentation(Allocator* allocator, size_t blocks) {
    fbl::Vector<ReservedExtent> extents[blocks];
    for (size_t i = 0; i < blocks; i++) {
        ASSERT_EQ(ZX_OK, allocator->ReserveBlocks(1, &extents[i]));
        ASSERT_EQ(1, extents[i].size());
    }

    for (size_t i = 0; i < blocks; i += 2) {
        allocator->MarkBlocksAllocated(extents[i][0]);
    }
}

// Save the extents within |in| in a non-reserved vector |out|.
void CopyExtents(const fbl::Vector<ReservedExtent>& in, fbl::Vector<Extent>* out) {
    out->reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        out->push_back(in[i].extent());
    }
}

// Save the nodes within |in| in a non-reserved vector |out|.
void CopyNodes(const fbl::Vector<ReservedNode>& in, fbl::Vector<uint32_t>* out) {
    out->reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        out->push_back(in[i].index());
    }
}

} // namespace blobfs
