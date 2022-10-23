// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/allocator/base_allocator.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/result.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include <bitmap/raw-bitmap.h>
#include <bitmap/rle-bitmap.h>
#include <safemath/safe_conversions.h>

#include "src/storage/blobfs/allocator/extent_reserver.h"
#include "src/storage/blobfs/allocator/node_reserver.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {

BaseAllocator::BaseAllocator(RawBitmap block_bitmap,
                             std::unique_ptr<id_allocator::IdAllocator> node_bitmap)
    : block_bitmap_(std::move(block_bitmap)), node_bitmap_(std::move(node_bitmap)) {}

bool BaseAllocator::CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                                         uint64_t* out_first_unallocated) const {
  size_t first_unallocated;
  bool result = block_bitmap_.Get(start_block, end_block, &first_unallocated);
  if (!result && out_first_unallocated != nullptr) {
    *out_first_unallocated = first_unallocated;
  }
  return result;
}

zx::result<bool> BaseAllocator::IsBlockAllocated(uint64_t block_number) const {
  if (block_number >= block_bitmap_.size()) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  return zx::ok(block_bitmap_.GetOne(block_number));
}

zx_status_t BaseAllocator::ReserveBlocks(uint64_t num_blocks,
                                         std::vector<ReservedExtent>* out_extents) {
  uint64_t actual_blocks;

  // TODO(smklein): If we allocate blocks up to the end of the block map, extend, and continue
  // allocating, we'll create two extents where one would suffice. If we knew how many reserved /
  // allocated blocks existed we could resize ahead-of-time and flatten this case, as an
  // optimization.

  if ((FindBlocks(0, num_blocks, out_extents, &actual_blocks) != ZX_OK)) {
    // If we have run out of blocks, attempt to add block slices via FVM. The new 'hint' is the
    // first location we could try to find blocks after merely extending the allocation maps.
    uint64_t hint = block_bitmap_.size() - std::min(num_blocks, uint64_t{block_bitmap_.size()});

    ZX_DEBUG_ASSERT(actual_blocks < num_blocks);
    num_blocks -= actual_blocks;

    if (AddBlocks(num_blocks).is_error() ||
        FindBlocks(hint, num_blocks, out_extents, &actual_blocks) != ZX_OK) {
      out_extents->clear();
      return ZX_ERR_NO_SPACE;
    }
  }
  return ZX_OK;
}

void BaseAllocator::MarkBlocksAllocated(const ReservedExtent& reserved_extent) {
  const Extent& extent = reserved_extent.extent();
  uint64_t start = extent.Start();
  uint64_t end = start + extent.Length();

  ZX_DEBUG_ASSERT(CheckBlocksUnallocated(start, end));
  ZX_ASSERT(block_bitmap_.Set(start, end) == ZX_OK);
}

ReservedExtent BaseAllocator::FreeBlocks(const Extent& extent) {
  uint64_t start = extent.Start();
  uint64_t end = start + extent.Length();

  ZX_DEBUG_ASSERT(CheckBlocksAllocated(start, end));
  ZX_ASSERT(block_bitmap_.Clear(start, end) == ZX_OK);

  // Keep the blocks reserved until freeing the blocks has been persisted.
  return ExtentReserver::Reserve(extent);
}

zx_status_t BaseAllocator::ReserveNodes(uint64_t num_nodes, std::vector<ReservedNode>* out_nodes) {
  for (uint64_t i = 0; i < num_nodes; i++) {
    zx::result<ReservedNode> node = ReserveNode();
    if (node.is_error()) {
      out_nodes->clear();
      return node.status_value();
    }
    out_nodes->push_back(std::move(node).value());
  }
  return ZX_OK;
}

zx::result<ReservedNode> BaseAllocator::ReserveNode() {
  zx::result<uint32_t> node = FindNode();
  if (node.is_error()) {
    // If we didn't find any free inodes, try adding more.
    if (zx::result<> status = AddNodes(); status.is_error()) {
      return zx::error(ZX_ERR_NO_SPACE);
    }
    node = FindNode();
    if (node.is_error()) {
      return node.take_error();
    }
  }
  ++reserved_node_count_;
  return zx::ok(ReservedNode(this, *node));
}

void BaseAllocator::MarkNodeAllocated(uint32_t node_index) {
  ZX_ASSERT(node_bitmap_->MarkAllocated(node_index) == ZX_OK);
}

void BaseAllocator::MarkInodeAllocated(ReservedNode node) {
  auto mapped_inode = GetNode(node.index());
  ZX_ASSERT_MSG(mapped_inode.is_ok(), "Failed to get a node that was reserved: %d",
                mapped_inode.status_value());
  ZX_ASSERT((mapped_inode->header.flags & kBlobFlagAllocated) == 0);
  mapped_inode->header.flags = kBlobFlagAllocated;
  // This value should not be relied upon as it is not part of the specification, it is chosen to
  // trigger crashing when used. This will be updated to a usable value when another node is
  // appended to the list.
  mapped_inode->header.next_node = kMaxNodeId;
  node.Release();
  --reserved_node_count_;
}

zx_status_t BaseAllocator::MarkContainerNodeAllocated(ReservedNode node,
                                                      uint32_t previous_node_index) {
  const uint32_t index = node.index();
  auto previous_node = GetNode(previous_node_index);
  if (previous_node.is_error()) {
    return previous_node.status_value();
  }
  previous_node->header.next_node = index;
  ExtentContainer* container = GetNode(index)->AsExtentContainer();
  ZX_ASSERT((container->header.flags & kBlobFlagAllocated) == 0);
  container->header.flags = kBlobFlagAllocated | kBlobFlagExtentContainer;
  // This value should not be relied upon as it is not part of the specification, it is chosen to
  // trigger crashing when used. This will be updated to a usable value when another node is
  // appended to the list.
  container->header.next_node = kMaxNodeId;
  container->previous_node = previous_node_index;
  container->extent_count = 0;
  node.Release();
  --reserved_node_count_;
  return ZX_OK;
}

zx_status_t BaseAllocator::FreeNode(uint32_t node_index) {
  auto node = GetNode(node_index);
  if (node.is_error()) {
    return node.status_value();
  }
  node->header.flags = 0;
  return node_bitmap_->Free(node_index);
}

void BaseAllocator::UnreserveNode(ReservedNode node) {
  zx_status_t status = node_bitmap_->Free(node.index());
  ZX_ASSERT_MSG(status == ZX_OK, "Failed to unreserve node: %d", status);
  node.Release();
  --reserved_node_count_;
}

uint64_t BaseAllocator::ReservedNodeCount() const { return reserved_node_count_; }

bool BaseAllocator::CheckBlocksUnallocated(uint64_t start_block, uint64_t end_block) const {
  ZX_DEBUG_ASSERT(end_block > start_block);
  size_t first_allocated;
  return block_bitmap_.Find(false, start_block, end_block, end_block - start_block,
                            &first_allocated) == ZX_OK;
}

bool BaseAllocator::FindUnallocatedExtent(uint64_t start, uint64_t block_length,
                                          uint64_t* out_start, uint64_t* out_block_length) {
  bool restart = false;
  // Constraint: No contiguous run which can extend beyond the block
  // bitmap.
  block_length = std::min(block_length, block_bitmap_.size() - start);
  size_t first_already_allocated;
  if (!block_bitmap_.Scan(start, start + block_length, false, &first_already_allocated)) {
    // Part of [start, start + block_length) is already allocated.
    if (first_already_allocated == start) {
      // Jump past as much of the allocated region as possible, and then restart searching for more
      // free blocks.
      size_t first_free;
      if (block_bitmap_.Scan(start, block_bitmap_.size(), true, &first_free)) {
        // All remaining blocks are already allocated.
        start = block_bitmap_.size();
      } else {
        // Not all blocks are allocated; jump to the first free block we can find.
        ZX_DEBUG_ASSERT(first_free > start);
        start = first_free;
      }
      // We recommend restarting the search in this case because although there was a prefix
      // collision, the suffix of this region may be followed by additional free blocks.
      restart = true;
    } else {
      // Since |start| is free, we'll try allocating from as much of this region as we can until we
      // hit previously-allocated blocks.
      ZX_DEBUG_ASSERT(first_already_allocated > start);
      block_length = first_already_allocated - start;
    }
  }

  *out_start = start;
  *out_block_length = block_length;
  return restart;
}

bool BaseAllocator::MunchUnreservedExtents(bitmap::RleBitmap::const_iterator reserved_iterator,
                                           uint64_t remaining_blocks, uint64_t start,
                                           uint64_t block_length,
                                           std::vector<ReservedExtent>* out_extents,
                                           bitmap::RleBitmap::const_iterator* out_reserved_iterator,
                                           uint64_t* out_remaining_blocks, uint64_t* out_start,
                                           uint64_t* out_block_length) {
  bool collision = false;

  const uint64_t start_max = start + block_length;

  // There are remaining in-flight reserved blocks we might collide with; verify this allocation is
  // not being held by another write operation.
  while ((start < start_max) && (block_length != 0) &&
         (reserved_iterator != ReservedBlocksCend())) {
    // We should only be considering blocks which are not allocated.
    ZX_DEBUG_ASSERT(start < start + block_length);
    ZX_DEBUG_ASSERT(block_bitmap_.Scan(start, start + block_length, false));

    if (reserved_iterator->end() <= start) {
      // The reserved iterator is lagging behind this region.
      ZX_DEBUG_ASSERT(reserved_iterator != ReservedBlocksCend());
      reserved_iterator++;
    } else if (start + block_length <= reserved_iterator->start()) {
      // The remaining reserved blocks occur after this free region; this allocation doesn't
      // collide.
      break;
    } else {
      // The reserved region ends at/after the start of the allocation. The reserved region starts
      // before the end of the allocation.
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
        // Free Prefix: Although the observed range overlaps with a reservation, it includes a
        // prefix which is free from overlap.
        //
        // Take the most of the proposed allocation *before* the reservation.
        Extent extent(start, reserved_iterator->start() - start);
        ZX_DEBUG_ASSERT(
            block_bitmap_.Scan(extent.Start(), extent.Start() + extent.Length(), false));
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
        out_extents->push_back(ExtentReserver::ReserveLocked(extent));
        reserved_iterator = ReservedBlocksCbegin();
      } else {
        // Free Suffix: The observed range overlaps with a reservation, but not entirely.
        //
        // Jump to the end of the reservation, as free space exists there.
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

zx_status_t BaseAllocator::FindBlocks(uint64_t start, uint64_t num_blocks,
                                      std::vector<ReservedExtent>* out_extents,
                                      uint64_t* out_actual_blocks) {
  std::scoped_lock lock(mutex());

  // Using a single iterator over the reserved allocation map lets us avoid re-scanning portions of
  // the reserved map. This is possible because the |reserved_blocks_| map should be immutable for
  // the duration of this method, unless we actually find blocks, at which point the iterator is
  // reset.
  auto reserved_iterator = ReservedBlocksCbegin();

  uint64_t remaining_blocks = num_blocks;
  while (remaining_blocks != 0) {
    // Look for |block_length| contiguous free blocks.
    if (start >= block_bitmap_.size()) {
      *out_actual_blocks = num_blocks - remaining_blocks;
      return ZX_ERR_NO_SPACE;
    }
    // Constraint: No contiguous run longer than the maximum permitted extent.
    uint64_t block_length = std::min(remaining_blocks, Extent::kBlockCountMax);

    bool restart_search = FindUnallocatedExtent(start, block_length, &start, &block_length);
    if (restart_search) {
      continue;
    }

    // [start, start + block_length) is now a valid region of free blocks.
    //
    // Take the subset of this range that doesn't intersect with reserved blocks, and add it to our
    // extent list.
    restart_search = MunchUnreservedExtents(reserved_iterator, remaining_blocks, start,
                                            block_length, out_extents, &reserved_iterator,
                                            &remaining_blocks, &start, &block_length);
    if (restart_search) {
      continue;
    }

    if (block_length != 0) {
      // The remainder of this window exists and does not collide with either the reservation map
      // nor the committed blocks.
      Extent extent(start, block_length);
      ZX_DEBUG_ASSERT(block_bitmap_.Scan(extent.Start(), extent.Start() + extent.Length(), false));
      start += extent.Length();
      remaining_blocks -= extent.Length();
      out_extents->push_back(ExtentReserver::ReserveLocked(extent));
      reserved_iterator = ReservedBlocksCbegin();
    }
  }

  *out_actual_blocks = num_blocks;
  return ZX_OK;
}

zx::result<uint32_t> BaseAllocator::FindNode() {
  size_t i;
  if (node_bitmap_->Allocate(&i) != ZX_OK) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  const uint32_t node_index = safemath::checked_cast<uint32_t>(i);
  auto node = GetNode(node_index);
  ZX_ASSERT_MSG(node.is_ok(), "Found a node that wasn't valid: %d", node.status_value());
  ZX_ASSERT_MSG(!node->header.IsAllocated(), "An unallocated node was marked as allocated");
  // Found a free node, which should not be reserved.
  return zx::ok(node_index);
}

std::vector<BlockRegion> BaseAllocator::GetAllocatedRegions() const {
  std::vector<BlockRegion> out_regions;
  size_t block_count = block_bitmap_.size();
  size_t offset = 0;
  size_t end = 0;
  while (!block_bitmap_.Scan(end, block_count, false, &offset)) {
    if (block_bitmap_.Scan(offset, block_count, true, &end)) {
      end = block_count;
    }
    out_regions.push_back({offset, end - offset});
  }
  return out_regions;
}

}  // namespace blobfs
