// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ALLOCATOR_ALLOCATOR_H_
#define SRC_STORAGE_BLOBFS_ALLOCATOR_ALLOCATOR_H_

#include <fuchsia/blobfs/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include <optional>
#include <shared_mutex>

#include <bitmap/raw-bitmap.h>
#include <bitmap/rle-bitmap.h>
#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/node-finder.h>
#include <fbl/algorithm.h>
#include <fbl/function.h>
#include <fbl/vector.h>
#include <fs/trace.h>
#include <fs/transaction/legacy_transaction_handler.h>
#include <id_allocator/id_allocator.h>
#include <storage/buffer/vmoid_registry.h>

#include "extent-reserver.h"
#include "node-reserver.h"

namespace blobfs {

class Allocator;

struct BlockRegion {
  uint64_t offset;
  uint64_t length;
};

// An interface which controls actual access to the underlying storage.
class SpaceManager : public storage::VmoidRegistry {
 public:
  virtual ~SpaceManager() = default;

  virtual const Superblock& Info() const = 0;

  // Adds any number of nodes to |allocator|'s node map, extending the volume if necessary.
  virtual zx_status_t AddInodes(Allocator* allocator) = 0;

  // Adds space for |nblocks| blocks to |map|, extending the volume if necessary.
  virtual zx_status_t AddBlocks(uint64_t nblocks, RawBitmap* map) = 0;
};

// Allocates and frees both block and node entries.
//
// Also maintains reservation mappings, to help in-progress allocations avoid
// from being persisted too early.
class Allocator : private ExtentReserver, private NodeReserver, public NodeFinder {
 public:
  Allocator(SpaceManager* space_manager, RawBitmap block_map, fzl::ResizeableVmoMapper node_map,
            std::unique_ptr<id_allocator::IdAllocator> nodes_bitmap);
  ~Allocator();

  using ExtentReserver::ReservedBlockCount;
  using NodeReserver::ReservedNodeCount;

  ////////////////
  // blobfs::NodeFinder interface.
  //
  // TODO(smklein): It may be possible to convert NodeFinder from an interface
  // to a concrete base class if we can reconcile the differences with host.

  InodePtr GetNode(uint32_t node_index) final;

  ////////////////
  // Other interfaces.

  void SetLogging(bool enable) { log_allocation_failure_ = enable; }

  // Returns true if [start_block, end_block) is allocated.
  //
  // If any blocks are unallocated, will set the optional output parameter
  // |out_first_unset| to the first unallocated block within this range.
  bool CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                            uint64_t* out_first_unset = nullptr) const;

  // Reads the block map and node map from underlying storage, using a
  // blocking read transaction.
  //
  // It is unsafe to call this method while any nodes or blocks are reserved.
  zx_status_t ResetFromStorage(fs::ReadTxn txn);

  // Provides a read-only view into the block map.
  const zx::vmo& GetBlockMapVmo() const;

  // Provides a read-only view into the node map.
  const zx::vmo& GetNodeMapVmo() const;

  // Reserves space for blocks in memory. Does not update disk.
  //
  // On success, appends the (possibly non-contiguous) region of allocated
  // blocks to |out_extents|.
  // On failure, |out_extents| is cleared.
  zx_status_t ReserveBlocks(uint64_t num_blocks, fbl::Vector<ReservedExtent>* out_extents);

  // Marks blocks as allocated which have previously been reserved to the bitmap.
  void MarkBlocksAllocated(const ReservedExtent& reserved_extent);

  // Frees blocks which have already been allocated (in-memory).
  //
  // |extent| must represent a portion of the block map which has already been
  // allocated.
  void FreeBlocks(const Extent& extent);

  // Reserves space for nodes in memory. Does not update disk.
  //
  // On success, appends the (possibly non-contiguous) nodes to |out_nodes|.
  // On failure, |out_nodes| is cleared.
  zx_status_t ReserveNodes(uint64_t num_nodes, fbl::Vector<ReservedNode>* out_nodes);

  // Reserves a nodes for allocation (in-memory).
  std::optional<ReservedNode> ReserveNode();

  // Marks a reserved node by updating the node map to indicate it is an
  // allocated inode.
  void MarkInodeAllocated(const ReservedNode& node);

  // Marks a reserved node by updating the node map to indicate it is an
  // allocated extent container
  void MarkContainerNodeAllocated(const ReservedNode& node, uint32_t previous_node);

  // Mark a node allocated. The node may or may not be reserved.
  void MarkNodeAllocated(uint32_t node_index);

  // Frees a node which has already been committed.
  void FreeNode(uint32_t node_index);

  // Record the location and size of all non-free block regions.
  fbl::Vector<BlockRegion> GetAllocatedRegions() const;

  // Called when InodePtr goes out of scope.
  void DropInodePtr() override;

  // Grows node map to |size|. The caller takes responsibility for initializing the new entries.
  [[nodiscard]] zx_status_t GrowNodeMap(size_t size);

 private:
  // Resets the size of the block map based on |Info().data_block_count|.
  //
  // It is unsafe to call this method while any blocks are reserved.
  zx_status_t ResetBlockMapSize();

  // Resets the size of the node map based on |Info().inode_count|.
  //
  // It is unsafe to call this method while any nodes are reserved.
  zx_status_t ResetNodeMapSize();

  // Returns true if [start_block, end_block) are unallocated.
  bool CheckBlocksUnallocated(uint64_t start_block, uint64_t end_block) const;

  // Avoids a collision with the committed block map, moving the starting
  // location / block length to find a region with no collision.
  //
  // Returns true if we should restart searching to attempt to maximally munch
  // from the allocation pool.
  bool FindUnallocatedExtent(uint64_t start, uint64_t block_length, uint64_t* out_start,
                             uint64_t* out_block_length);

  // Identifies the subset of blocks which don't collide with pending
  // reservations. If any collisions exist, maximally munches the available
  // free space into newly reserved extents.
  //
  // It is assumed that [start, start + block_length) is unallocated;
  // this is internally asserted. |FindUnallocatedExtent| should be invoked
  // first to provide this guarantee.
  //
  // Returns true if we should restart searching to attempt to maximally munch
  // from the allocation pool. Otherwise, no collisions with pending
  // reservations exist.
  bool MunchUnreservedExtents(bitmap::RleBitmap::const_iterator reserved_iterator,
                              uint64_t remaining_blocks, uint64_t start, uint64_t block_length,
                              fbl::Vector<ReservedExtent>* out_extents,
                              bitmap::RleBitmap::const_iterator* out_reserved_iterator,
                              uint64_t* out_remaining_blocks, uint64_t* out_start,
                              uint64_t* out_block_length);

  // Searches for |nblocks| free blocks between the block_map_ and reserved_blocks_ bitmaps.
  //
  // Appends the (possibly non-contiguous) region of allocated blocks to |out_extents|.
  //
  // May fail if not enough blocks can be found. In this case, an error will be returned,
  // and the number of found blocks will be returned in |out_actual_blocks|. This result
  // is guaranteed to be less than or equal to |num_blocks|.
  zx_status_t FindBlocks(uint64_t start, uint64_t num_blocks,
                         fbl::Vector<ReservedExtent>* out_extents, uint64_t* out_actual_blocks);

  zx_status_t FindNode(uint32_t* node_index_out);

  void LogAllocationFailure(uint64_t num_blocks) const;

  // Grow allocator
  zx_status_t Grow();

  SpaceManager* space_manager_;

  RawBitmap block_map_ = {};
  fzl::ResizeableVmoMapper node_map_;
  // Guards growing node_map_, which will invalidate outstanding pointers.
  std::shared_mutex node_map_grow_mutex_;
  std::unique_ptr<id_allocator::IdAllocator> node_bitmap_;

  bool log_allocation_failure_ = true;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_ALLOCATOR_ALLOCATOR_H_
