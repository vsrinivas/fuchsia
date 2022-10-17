// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ALLOCATOR_BASE_ALLOCATOR_H_
#define SRC_STORAGE_BLOBFS_ALLOCATOR_BASE_ALLOCATOR_H_

#include <lib/stdcompat/span.h>

#include <cstdint>
#include <vector>

#include <bitmap/raw-bitmap.h>
#include <id_allocator/id_allocator.h>

#include "src/storage/blobfs/allocator/extent_reserver.h"
#include "src/storage/blobfs/allocator/node_reserver.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {

struct BlockRegion {
  uint64_t offset;
  uint64_t length;
};

// Base class for managing the blobfs block bitmap and node allocations. Allows for reserving blocks
// and nodes without updating the allocations so the reservations are not persisted prematurely.
class BaseAllocator : private ExtentReserver, private NodeReserverInterface, public NodeFinder {
 public:
  using ExtentReserver::ReservedBlockCount;

  BaseAllocator(RawBitmap block_bitmap, std::unique_ptr<id_allocator::IdAllocator> node_bitmap);

  ~BaseAllocator() override = default;

  // Returns true if [start_block, end_block) is allocated.
  //
  // If any blocks are unallocated, will set the optional output parameter |out_first_unset| to
  // the first unallocated block within this range.
  bool CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                            uint64_t* out_first_unallocated = nullptr) const;

  // Returns true if the block is allocated. If the block_number is invalid, returns error.
  zx::result<bool> IsBlockAllocated(uint64_t block_number) const;

  // Reserves space for blocks in memory. Does not update disk.
  //
  // On success, appends the (possibly non-contiguous) region of allocated blocks to |out_extents|.
  // On failure, |out_extents| is cleared.
  zx_status_t ReserveBlocks(uint64_t num_blocks, std::vector<ReservedExtent>* out_extents);

  // Marks blocks as allocated which have previously been reserved.
  void MarkBlocksAllocated(const ReservedExtent& reserved_extent);

  // Frees blocks which have already been allocated (in-memory).
  //
  // |extent| must represent a portion of the block map which has already been allocated.  Returns a
  // ReservedExtent which keeps the blocks reserved until destroyed (which allows us to hold the
  // blocks until the transaction commits).
  ReservedExtent FreeBlocks(const Extent& extent);

  // Reserves space for nodes in memory. Does not update disk.
  //
  // On success, appends the (possibly non-contiguous) nodes to |out_nodes|. On failure, |out_nodes|
  // is cleared.
  zx_status_t ReserveNodes(uint64_t num_nodes, std::vector<ReservedNode>* out_nodes);

  // blobfs::NodeReserverInterface interface.
  zx::result<ReservedNode> ReserveNode() override;
  void UnreserveNode(ReservedNode node) override;
  uint64_t ReservedNodeCount() const override;

  // Marks a reserved node by updating the node map to indicate it is an allocated inode.
  void MarkInodeAllocated(ReservedNode node);

  // Marks a reserved node by updating the node map to indicate it is an allocated extent container.
  // Makes |node| follow |previous_node_index| in the extent container list.
  zx_status_t MarkContainerNodeAllocated(ReservedNode node, uint32_t previous_node_index);

  // Mark a node allocated. The node may or may not be reserved.
  void MarkNodeAllocated(uint32_t node_index);

  // Frees a node which has already been committed. Returns an error if the node could not be freed.
  zx_status_t FreeNode(uint32_t node_index);

  // Record the location and size of all non-free block regions.
  std::vector<BlockRegion> GetAllocatedRegions() const;

 protected:
  // Requests that blobfs increase the size of it's data section by |block_count| blocks.
  virtual zx::result<> AddBlocks(uint64_t block_count) = 0;

  // Requests that blobfs increase the size of it's node map.
  virtual zx::result<> AddNodes() = 0;

  RawBitmap& GetBlockBitmap() { return block_bitmap_; }
  const RawBitmap& GetBlockBitmap() const { return block_bitmap_; }
  id_allocator::IdAllocator& GetNodeBitmap() { return *node_bitmap_; }
  const id_allocator::IdAllocator& GetNodeBitmap() const { return *node_bitmap_; }

 private:
  // Returns true if [start_block, end_block) are unallocated.
  bool CheckBlocksUnallocated(uint64_t start_block, uint64_t end_block) const;

  // Avoids a collision with the committed block map, moving the starting location / block length to
  // find a region with no collision.
  //
  // Returns true if we should restart searching to attempt to maximally munch from the allocation
  // pool.
  bool FindUnallocatedExtent(uint64_t start, uint64_t block_length, uint64_t* out_start,
                             uint64_t* out_block_length);

  // Identifies the subset of blocks which don't collide with pending reservations. If any
  // collisions exist, maximally munches the available free space into newly reserved extents.
  //
  // It is assumed that [start, start + block_length) is unallocated; this is internally asserted.
  // |FindUnallocatedExtent| should be invoked first to provide this guarantee.
  //
  // Returns true if we should restart searching to attempt to maximally munch from the allocation
  // pool. Otherwise, no collisions with pending reservations exist.
  bool MunchUnreservedExtents(bitmap::RleBitmap::const_iterator reserved_iterator,
                              uint64_t remaining_blocks, uint64_t start, uint64_t block_length,
                              std::vector<ReservedExtent>* out_extents,
                              bitmap::RleBitmap::const_iterator* out_reserved_iterator,
                              uint64_t* out_remaining_blocks, uint64_t* out_start,
                              uint64_t* out_block_length) __TA_REQUIRES(mutex());

  // Searches for |num_blocks| free blocks between the block_map_ and reserved_blocks_ bitmaps.
  //
  // Appends the (possibly non-contiguous) region of allocated blocks to |out_extents|.
  //
  // May fail if not enough blocks can be found. In this case, an error will be returned, and the
  // number of found blocks will be returned in |out_actual_blocks|. This result is guaranteed to be
  // less than or equal to |num_blocks|.
  zx_status_t FindBlocks(uint64_t start, uint64_t num_blocks,
                         std::vector<ReservedExtent>* out_extents, uint64_t* out_actual_blocks);

  // Finds an unallocated node.
  zx::result<uint32_t> FindNode();

  // The number of nodes currently reserved.
  uint64_t reserved_node_count_ = 0;
  RawBitmap block_bitmap_;
  std::unique_ptr<id_allocator::IdAllocator> node_bitmap_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_ALLOCATOR_BASE_ALLOCATOR_H_
