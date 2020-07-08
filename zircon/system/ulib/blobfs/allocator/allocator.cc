// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "allocator.h"

#include <inttypes.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <zircon/types.h>

#include <algorithm>

#include <bitmap/raw-bitmap.h>
#include <bitmap/rle-bitmap.h>
#include <blobfs/common.h>
#include <blobfs/format.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/vector.h>
#include <fs/trace.h>
#include <storage/buffer/owned_vmoid.h>

#include "extent-reserver.h"
#include "iterator/extent-iterator.h"
#include "node-reserver.h"

namespace blobfs {

Allocator::Allocator(SpaceManager* space_manager, RawBitmap block_map,
                     fzl::ResizeableVmoMapper node_map,
                     std::unique_ptr<id_allocator::IdAllocator> nodes_bitmap)
    : space_manager_(space_manager),
      block_map_(std::move(block_map)),
      node_map_(std::move(node_map)),
      node_bitmap_(std::move(nodes_bitmap)) {}

Allocator::~Allocator() = default;

InodePtr Allocator::GetNode(uint32_t node_index) {
  ZX_DEBUG_ASSERT(node_index < node_map_.size() / kBlobfsInodeSize);
  node_map_grow_mutex_.lock_shared();
  return InodePtr(&reinterpret_cast<Inode*>(node_map_.start())[node_index], InodePtrDeleter(this));
}

bool Allocator::CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                                     uint64_t* out_first_unset) const {
  uint64_t unset_bit;
  bool allocated = block_map_.Get(start_block, end_block, &unset_bit);
  if (!allocated && out_first_unset != nullptr) {
    *out_first_unset = unset_bit;
  }
  return allocated;
}

zx_status_t Allocator::ResetFromStorage(fs::ReadTxn txn) {
  ZX_DEBUG_ASSERT(ReservedBlockCount() == 0);
  ZX_DEBUG_ASSERT(ReservedNodeCount() == 0);

  // Ensure the block and node maps are up-to-date with changes in size that
  // might have happened.
  zx_status_t status;
  if ((status = ResetBlockMapSize()) != ZX_OK) {
    return status;
  }

  if ((status = ResetNodeMapSize()) != ZX_OK) {
    return status;
  }

  storage::OwnedVmoid block_map_vmoid;
  storage::OwnedVmoid node_map_vmoid;

  // TODO(fxb/49093): Change to use fit::result<OwnedVmo, zx_status_t>.
  status = space_manager_->BlockAttachVmo(block_map_.StorageUnsafe()->GetVmo(),
                                          &block_map_vmoid.GetReference(space_manager_));
  if (status != ZX_OK) {
    return status;
  }

  status =
      space_manager_->BlockAttachVmo(node_map_.vmo(), &node_map_vmoid.GetReference(space_manager_));
  if (status != ZX_OK) {
    return status;
  }

  const auto info = space_manager_->Info();
  txn.Enqueue(block_map_vmoid.get(), 0, BlockMapStartBlock(info), BlockMapBlocks(info));
  txn.Enqueue(node_map_vmoid.get(), 0, NodeMapStartBlock(info), NodeMapBlocks(info));

  return txn.Transact();
}

const zx::vmo& Allocator::GetBlockMapVmo() const { return block_map_.StorageUnsafe()->GetVmo(); }

const zx::vmo& Allocator::GetNodeMapVmo() const { return node_map_.vmo(); }

zx_status_t Allocator::ReserveBlocks(uint64_t num_blocks,
                                     fbl::Vector<ReservedExtent>* out_extents) {
  zx_status_t status;
  uint64_t actual_blocks;

  // TODO(smklein): If we allocate blocks up to the end of the block map, extend, and continue
  // allocating, we'll create two extents where one would suffice.
  // If we knew how many reserved / allocated blocks existed we could resize ahead-of-time and
  // flatten this case, as an optimization.

  if ((status = FindBlocks(0, num_blocks, out_extents, &actual_blocks) != ZX_OK)) {
    // If we have run out of blocks, attempt to add block slices via FVM.
    // The new 'hint' is the first location we could try to find blocks
    // after merely extending the allocation maps.
    uint64_t hint = block_map_.size() - std::min(num_blocks, block_map_.size());

    ZX_DEBUG_ASSERT(actual_blocks < num_blocks);
    num_blocks -= actual_blocks;

    if ((status = space_manager_->AddBlocks(num_blocks, &block_map_) != ZX_OK) ||
        (status = FindBlocks(hint, num_blocks, out_extents, &actual_blocks)) != ZX_OK) {
      LogAllocationFailure(num_blocks);
      out_extents->reset();
      return ZX_ERR_NO_SPACE;
    }
  }
  return ZX_OK;
}

void Allocator::MarkBlocksAllocated(const ReservedExtent& reserved_extent) {
  const Extent& extent = reserved_extent.extent();
  uint64_t start = extent.Start();
  uint64_t length = extent.Length();
  uint64_t end = start + length;

  ZX_DEBUG_ASSERT(CheckBlocksUnallocated(start, end));
  ZX_ASSERT(block_map_.Set(start, end) == ZX_OK);
}

void Allocator::FreeBlocks(const Extent& extent) {
  uint64_t start = extent.Start();
  uint64_t length = extent.Length();
  uint64_t end = start + length;

  ZX_DEBUG_ASSERT(CheckBlocksAllocated(start, end));
  ZX_ASSERT(block_map_.Clear(start, end) == ZX_OK);
}

zx_status_t Allocator::ReserveNodes(uint64_t num_nodes, fbl::Vector<ReservedNode>* out_nodes) {
  for (uint64_t i = 0; i < num_nodes; i++) {
    std::optional<ReservedNode> node = ReserveNode();
    if (!node) {
      out_nodes->reset();
      return ZX_ERR_NO_SPACE;
    }
    out_nodes->push_back(*std::move(node));
  }
  return ZX_OK;
}

zx_status_t Allocator::Grow() {
  zx_status_t status = space_manager_->AddInodes(this);
  if (status != ZX_OK) {
    return status;
  }

  auto inode_count = space_manager_->Info().inode_count;
  status = node_bitmap_->Grow(inode_count);
  // This is awkward situation where we could secure storage but potentially
  // ran out of [virtual] memory. There is nothing much we can do. The filesystem
  // might fail soon from other alloc failures. It is better to turn the fs-mount
  // into read-only instance or panic to safe-guard against any damage rather than
  // propogating these errors.
  //
  // One alternative considered was to reorder memory allocation first and then
  // allocate disk. Reordering just delays the problem and also to reorder this
  // layer needs to know details like what is fvm slice size is. We decided
  // against that route.
  if (status != ZX_OK) {
    fprintf(stderr, "blobfs: Failed to grow bitmap for inodes\n");
  }
  return status;
}

std::optional<ReservedNode> Allocator::ReserveNode() {
  TRACE_DURATION("blobfs", "ReserveNode");
  uint32_t node_index;
  zx_status_t status;
  if ((status = FindNode(&node_index)) != ZX_OK) {
    // If we didn't find any free inodes, try adding more via FVM.
    if (((status = Grow()) != ZX_OK) || (status = FindNode(&node_index)) != ZX_OK) {
      return std::nullopt;
    }
  }

  ZX_DEBUG_ASSERT(!GetNode(node_index)->header.IsAllocated());
  std::optional<ReservedNode> node(ReservedNode(this, node_index));

  return node;
}

void Allocator::MarkNodeAllocated(uint32_t node_index) {
  ZX_ASSERT(node_bitmap_->MarkAllocated(node_index) == ZX_OK);
}

void Allocator::MarkInodeAllocated(const ReservedNode& node) {
  InodePtr mapped_inode = GetNode(node.index());
  ZX_ASSERT((mapped_inode->header.flags & kBlobFlagAllocated) == 0);
  mapped_inode->header.flags = kBlobFlagAllocated;
  // This value should not be relied upon as it is not part of the
  // specification, it is chosen to trigger crashing when used. This will be
  // updated to a usable value when another node is appended to the list.
  mapped_inode->header.next_node = kMaxNodeId;
}

void Allocator::MarkContainerNodeAllocated(const ReservedNode& node, uint32_t previous_node) {
  const uint32_t index = node.index();
  GetNode(previous_node)->header.next_node = index;
  ExtentContainer* container = GetNode(index)->AsExtentContainer();
  ZX_ASSERT((container->header.flags & kBlobFlagAllocated) == 0);
  container->header.flags = kBlobFlagAllocated | kBlobFlagExtentContainer;
  // This value should not be relied upon as it is not part of the
  // specification, it is chosen to trigger crashing when used. This will be
  // updated to a usable value when another node is appended to the list.
  container->header.next_node = kMaxNodeId;
  container->previous_node = previous_node;
  container->extent_count = 0;
}

void Allocator::FreeNode(uint32_t node_index) {
  GetNode(node_index)->header.flags = 0;
  ZX_ASSERT(node_bitmap_->Free(node_index) == ZX_OK);
}

zx_status_t Allocator::ResetBlockMapSize() {
  ZX_DEBUG_ASSERT(ReservedBlockCount() == 0);
  const auto info = space_manager_->Info();
  uint64_t new_size = info.data_block_count;
  if (new_size != block_map_.size()) {
    uint64_t rounded_size = BlockMapBlocks(info) * kBlobfsBlockBits;
    zx_status_t status = block_map_.Reset(rounded_size);
    if (status != ZX_OK) {
      return status;
    }

    if (new_size < rounded_size) {
      // In the event that the requested block count is not a multiple
      // of the nearest block size, shrink down to the actual block
      // count.
      status = block_map_.Shrink(new_size);
      if (status != ZX_OK) {
        return status;
      }
    }
  }
  return ZX_OK;
}

zx_status_t Allocator::ResetNodeMapSize() {
  ZX_DEBUG_ASSERT(ReservedNodeCount() == 0);
  const auto info = space_manager_->Info();
  uint64_t nodemap_size = kBlobfsInodeSize * info.inode_count;
  zx_status_t status = ZX_OK;
  if (fbl::round_up(nodemap_size, kBlobfsBlockSize) != nodemap_size) {
    return ZX_ERR_BAD_STATE;
  }
  ZX_DEBUG_ASSERT(nodemap_size / kBlobfsBlockSize == NodeMapBlocks(info));

  if (nodemap_size > node_map_.size()) {
    status = GrowNodeMap(nodemap_size);
  } else if (nodemap_size < node_map_.size()) {
    // It is safe to shrink node_map_ without a lock because the mapping won't change in that case.
    status = node_map_.Shrink(nodemap_size);
  }
  if (status != ZX_OK) {
    return status;
  }
  return node_bitmap_->Reset(info.inode_count);
}

bool Allocator::CheckBlocksUnallocated(uint64_t start_block, uint64_t end_block) const {
  ZX_DEBUG_ASSERT(end_block > start_block);
  uint64_t blkno_out;
  return block_map_.Find(false, start_block, end_block, end_block - start_block, &blkno_out) ==
         ZX_OK;
}

bool Allocator::FindUnallocatedExtent(uint64_t start, uint64_t block_length, uint64_t* out_start,
                                      uint64_t* out_block_length) {
  bool restart = false;
  // Constraint: No contiguous run which can extend beyond the block
  // bitmap.
  block_length = std::min(block_length, block_map_.size() - start);
  uint64_t first_already_allocated;
  if (!block_map_.Scan(start, start + block_length, false, &first_already_allocated)) {
    // Part of [start, start + block_length) is already allocated.
    if (first_already_allocated == start) {
      // Jump past as much of the allocated region as possible,
      // and then restart searching for more free blocks.
      uint64_t first_free;
      if (block_map_.Scan(start, start + block_length, true, &first_free)) {
        // All bits are allocated; jump past this entire portion
        // of allocated blocks.
        start += block_length;
      } else {
        // Not all blocks are allocated; jump to the first free block we can find.
        ZX_DEBUG_ASSERT(first_free > start);
        start = first_free;
      }
      // We recommend restarting the search in this case because
      // although there was a prefix collision, the suffix of this
      // region may be followed by additional free blocks.
      restart = true;
    } else {
      // Since |start| is free, we'll try allocating from as much of this region
      // as we can until we hit previously-allocated blocks.
      ZX_DEBUG_ASSERT(first_already_allocated > start);
      block_length = first_already_allocated - start;
    }
  }

  *out_start = start;
  *out_block_length = block_length;
  return restart;
}

bool Allocator::MunchUnreservedExtents(bitmap::RleBitmap::const_iterator reserved_iterator,
                                       uint64_t remaining_blocks, uint64_t start,
                                       uint64_t block_length,
                                       fbl::Vector<ReservedExtent>* out_extents,
                                       bitmap::RleBitmap::const_iterator* out_reserved_iterator,
                                       uint64_t* out_remaining_blocks, uint64_t* out_start,
                                       uint64_t* out_block_length) {
  bool collision = false;

  const uint64_t start_max = start + block_length;

  // There are remaining in-flight reserved blocks we might collide with;
  // verify this allocation is not being held by another write operation.
  while ((start < start_max) && (block_length != 0) &&
         (reserved_iterator != ReservedBlocksCend())) {
    // We should only be considering blocks which are not allocated.
    ZX_DEBUG_ASSERT(start < start + block_length);
    ZX_DEBUG_ASSERT(block_map_.Scan(start, start + block_length, false));

    if (reserved_iterator->end() <= start) {
      // The reserved iterator is lagging behind this region.
      ZX_DEBUG_ASSERT(reserved_iterator != ReservedBlocksCend());
      reserved_iterator++;
    } else if (start + block_length <= reserved_iterator->start()) {
      // The remaining reserved blocks occur after this free region;
      // this allocation doesn't collide.
      break;
    } else {
      // The reserved region ends at/after the start of the allocation.
      // The reserved region starts before the end of the allocation.
      //
      // This implies a collision exists.
      collision = true;
      if (start >= reserved_iterator->start() && start + block_length <= reserved_iterator->end()) {
        // The collision is total; move past the entire reserved region.
        start = reserved_iterator->end();
        block_length = 0;
        break;
      }
      if (start < reserved_iterator->start()) {
        // Free Prefix: Although the observed range overlaps with a
        // reservation, it includes a prefix which is free from
        // overlap.
        //
        // Take the most of the proposed allocation *before* the reservation.
        Extent extent(start, static_cast<BlockCountType>(reserved_iterator->start() - start));
        ZX_DEBUG_ASSERT(block_map_.Scan(extent.Start(), extent.Start() + extent.Length(), false));
        ZX_DEBUG_ASSERT(block_length > extent.Length());
        // Jump past the end of this reservation.
        uint64_t reserved_length = reserved_iterator->end() - reserved_iterator->start();
        if (block_length > extent.Length() + reserved_length) {
          block_length -= extent.Length() + reserved_length;
        } else {
          block_length = 0;
        }
        start = reserved_iterator->end();
        remaining_blocks -= extent.Length();
        out_extents->push_back(ReservedExtent(this, std::move(extent)));
        reserved_iterator = ReservedBlocksCbegin();
      } else {
        // Free Suffix: The observed range overlaps with a
        // reservation, but not entirely.
        //
        // Jump to the end of the reservation, as free space exists
        // there.
        ZX_DEBUG_ASSERT(start + block_length > reserved_iterator->end());
        block_length = (start + block_length) - reserved_iterator->end();
        start = reserved_iterator->end();
      }
    }
  }

  *out_remaining_blocks = remaining_blocks;
  *out_reserved_iterator = reserved_iterator;
  *out_start = start;
  *out_block_length = block_length;
  return collision;
}

zx_status_t Allocator::FindBlocks(uint64_t start, uint64_t num_blocks,
                                  fbl::Vector<ReservedExtent>* out_extents,
                                  uint64_t* out_actual_blocks) {
  // Using a single iterator over the reserved allocation map lets us
  // avoid re-scanning portions of the reserved map. This is possible
  // because the |reserved_blocks_| map should be immutable
  // for the duration of this method, unless we actually find blocks, at
  // which point the iterator is reset.
  auto reserved_iterator = ReservedBlocksCbegin();

  uint64_t remaining_blocks = num_blocks;
  while (remaining_blocks != 0) {
    // Look for |block_length| contiguous free blocks.
    if (start >= block_map_.size()) {
      *out_actual_blocks = num_blocks - remaining_blocks;
      return ZX_ERR_NO_SPACE;
    }
    // Constraint: No contiguous run longer than the maximum permitted
    // extent.
    uint64_t block_length = std::min(remaining_blocks, kBlockCountMax);

    bool restart_search = FindUnallocatedExtent(start, block_length, &start, &block_length);
    if (restart_search) {
      continue;
    }

    // [start, start + block_length) is now a valid region of free blocks.
    //
    // Take the subset of this range that doesn't intersect with reserved blocks,
    // and add it to our extent list.
    restart_search = MunchUnreservedExtents(reserved_iterator, remaining_blocks, start,
                                            block_length, out_extents, &reserved_iterator,
                                            &remaining_blocks, &start, &block_length);
    if (restart_search) {
      continue;
    }

    if (block_length != 0) {
      // The remainder of this window exists and does not collide with either
      // the reservation map nor the committed blocks.
      Extent extent(start, static_cast<BlockCountType>(block_length));
      ZX_DEBUG_ASSERT(block_map_.Scan(extent.Start(), extent.Start() + extent.Length(), false));
      start += extent.Length();
      remaining_blocks -= extent.Length();
      out_extents->push_back(ReservedExtent(this, std::move(extent)));
      reserved_iterator = ReservedBlocksCbegin();
    }
  }

  *out_actual_blocks = num_blocks;
  return ZX_OK;
}

zx_status_t Allocator::FindNode(uint32_t* node_index_out) {
  size_t i;
  if (node_bitmap_->Allocate(&i) != ZX_OK) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  ZX_ASSERT(i <= UINT32_MAX);
  uint32_t node_index = static_cast<uint32_t>(i);
  ZX_ASSERT(!GetNode(node_index)->header.IsAllocated());
  // Found a free node, which should not be reserved.
  ZX_ASSERT(!IsNodeReserved(node_index));
  *node_index_out = node_index;
  return ZX_OK;
}

void Allocator::LogAllocationFailure(uint64_t num_blocks) const {
  const Superblock& info = space_manager_->Info();
  const uint64_t requested_bytes = num_blocks * info.block_size;
  const uint64_t total_bytes = info.data_block_count * info.block_size;
  const uint64_t persisted_used_bytes = info.alloc_block_count * info.block_size;
  const uint64_t pending_used_bytes = ReservedBlockCount() * info.block_size;
  const uint64_t used_bytes = persisted_used_bytes + pending_used_bytes;
  ZX_ASSERT_MSG(used_bytes <= total_bytes,
                "blobfs using more bytes than available: %" PRIu64 " > %" PRIu64 "\n", used_bytes,
                total_bytes);
  const uint64_t free_bytes = total_bytes - used_bytes;

  if (!log_allocation_failure_) {
    return;
  }

  FS_TRACE_ERROR("Blobfs has run out of space on persistent storage.\n");
  FS_TRACE_ERROR("    Could not allocate %" PRIu64 " bytes\n", requested_bytes);
  FS_TRACE_ERROR("    Total data bytes  : %" PRIu64 "\n", total_bytes);
  FS_TRACE_ERROR("    Used data bytes   : %" PRIu64 "\n", persisted_used_bytes);
  FS_TRACE_ERROR("    Preallocated bytes: %" PRIu64 "\n", pending_used_bytes);
  FS_TRACE_ERROR("    Free data bytes   : %" PRIu64 "\n", free_bytes);
  FS_TRACE_ERROR("    This allocation failure is the result of %s.\n",
                 requested_bytes <= free_bytes ? "fragmentation" : "over-allocation");
}

// Finds all allocated regions in the bitmap and returns a vector of their offsets and lengths.
fbl::Vector<BlockRegion> Allocator::GetAllocatedRegions() const {
  fbl::Vector<BlockRegion> out_regions;
  uint64_t offset = 0;
  uint64_t end = 0;
  while (!block_map_.Scan(end, block_map_.size(), false, &offset)) {
    if (block_map_.Scan(offset, block_map_.size(), true, &end)) {
      end = block_map_.size();
    }
    out_regions.push_back({offset, end - offset});
  }
  return out_regions;
}

zx_status_t Allocator::GrowNodeMap(size_t size) {
  std::scoped_lock lock(node_map_grow_mutex_);
  return node_map_.Grow(size);
}

void Allocator::DropInodePtr() { node_map_grow_mutex_.unlock_shared(); }

}  // namespace blobfs
