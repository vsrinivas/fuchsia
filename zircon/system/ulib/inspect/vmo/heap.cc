// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/vmo/heap.h>
#include <zircon/process.h>

namespace inspect {
namespace internal {

namespace {
// Get the "buddy" for a given |Block|. Buddies may be merged together if they are both free.
constexpr BlockIndex Buddy(BlockIndex block, BlockOrder block_order) {
  // XOR index of the block by its size (in kMinOrderSize blocks) to get the index of its
  // buddy.
  return block ^ IndexForOffset(OrderToSize(block_order));
}

}  // namespace

Heap::Heap(zx::vmo vmo) : vmo_(std::move(vmo)) {
  ZX_ASSERT(ZX_OK == vmo_.get_size(&max_size_));
  ZX_ASSERT(max_size_ > 0);
  ZX_ASSERT(ZX_OK == zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
                                 vmo_.get(), 0, max_size_, &buffer_addr_));

  Extend(kMinVmoSize);
}

Heap::~Heap() {
  zx_vmar_unmap(zx_vmar_root_self(), buffer_addr_, max_size_);
  ZX_DEBUG_ASSERT_MSG(num_allocated_blocks_ == 0, "There are still %lu outstanding blocks",
                      num_allocated_blocks_);
}

const zx::vmo& Heap::GetVmo() const { return vmo_; }

zx_status_t Heap::Allocate(size_t min_size, BlockIndex* out_block) {
  ZX_DEBUG_ASSERT_MSG(min_size >= sizeof(Block), "Block allocation size %lu is too small",
                      min_size);
  const BlockOrder min_fit_order = FitOrder(min_size);
  ZX_DEBUG_ASSERT_MSG(min_fit_order < kNumOrders, "Order %u is greater than maximum order %lu",
                      min_fit_order, kNumOrders - 1);
  if (min_fit_order >= kNumOrders) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Iterate through the orders until we find a free block with order >=
  // what is needed.
  BlockOrder next_order = kNumOrders;
  for (BlockOrder i = min_fit_order; i < kNumOrders; i++) {
    if (IsFreeBlock(free_blocks_[i], i)) {
      next_order = i;
      break;
    }
  }

  // If no free block is found, extend the VMO and use one of the newly
  // created free blocks.
  if (next_order == kNumOrders) {
    zx_status_t status;
    status = Extend(cur_size_ * 2);
    if (status != ZX_OK) {
      return status;
    }
    next_order = kNumOrders - 1;
    ZX_ASSERT(IsFreeBlock(free_blocks_[kNumOrders - 1], kNumOrders - 1));
  }

  // Once a free block is found, split it repeatedly until it is the
  // right size.
  BlockIndex next_block_index = free_blocks_[next_order];
  while (GetOrder(GetBlock(next_block_index)) > min_fit_order) {
    if (!SplitBlock(next_block_index)) {
      return ZX_ERR_INTERNAL;
    }
  }

  // Remove the block from the free list, clear, and reserve it.
  RemoveFree(next_block_index);
  auto* next_block = GetBlock(next_block_index);

  next_block->header = BlockFields::Order::Make(GetOrder(next_block)) |
                       BlockFields::Type::Make(BlockType::kReserved);

  *out_block = next_block_index;
  ++num_allocated_blocks_;
  return ZX_OK;
}

void Heap::Free(BlockIndex block_index) {
  auto* block = GetBlock(block_index);
  BlockIndex buddy_index = Buddy(block_index, GetOrder(block));
  auto* buddy = GetBlock(buddy_index);

  // Repeatedly merge buddies of the freed block until the buddy is
  // not free or we hit the maximum block size.
  while (GetType(buddy) == BlockType::kFree && GetOrder(block) < kNumOrders - 1 &&
         GetOrder(block) == GetOrder(buddy)) {
    RemoveFree(buddy_index);
    if (buddy < block) {
      // We must always merge into the lower index block.
      // If the buddy of the block is a lower index, we need to swap
      // index and pointers.
      std::swap(block, buddy);
      std::swap(block_index, buddy_index);
    }
    BlockFields::Order::Set(&block->header, GetOrder(block) + 1);
    buddy_index = Buddy(block_index, GetOrder(block));
    buddy = GetBlock(buddy_index);
  }

  // Complete freeing the block by linking it onto the free list.
  block->header = BlockFields::Order::Make(GetOrder(block)) |
                  BlockFields::Type::Make(BlockType::kFree) |
                  FreeBlockFields::NextFreeBlock::Make(free_blocks_[GetOrder(block)]);
  free_blocks_[GetOrder(block)] = block_index;
  --num_allocated_blocks_;
}

bool Heap::SplitBlock(BlockIndex block) {
  RemoveFree(block);
  auto* cur = GetBlock(block);
  BlockOrder order = GetOrder(cur);
  ZX_DEBUG_ASSERT_MSG(order < kNumOrders, "Order on block is invalid");
  if (order >= kNumOrders) {
    return false;
  }

  // Lower the order of the original block, then find its new buddy.
  // Both the original block and the new buddy need to be added
  // onto the free list of the new order.
  BlockIndex buddy_index = Buddy(block, order - 1);
  auto* buddy = GetBlock(buddy_index);
  cur->header = BlockFields::Order::Make(order - 1) | BlockFields::Type::Make(BlockType::kFree) |
                FreeBlockFields::NextFreeBlock::Make(buddy_index);

  buddy->header = BlockFields::Order::Make(order - 1) | BlockFields::Type::Make(BlockType::kFree) |
                  FreeBlockFields::NextFreeBlock::Make(free_blocks_[order - 1]);

  free_blocks_[order - 1] = block;

  return true;
}

bool Heap::RemoveFree(BlockIndex block) {
  auto* to_remove = GetBlock(block);
  BlockOrder order = GetOrder(to_remove);
  ZX_DEBUG_ASSERT_MSG(order < kNumOrders, "Order %u on block %lu is invalid", order, block);
  if (order >= kNumOrders) {
    return false;
  }

  // If the block we are removing is at the head of the list,
  // immediately unlink it and return.
  BlockIndex next = free_blocks_[order];
  if (next == block) {
    free_blocks_[order] = FreeBlockFields::NextFreeBlock::Get<size_t>(to_remove->header);
    return true;
  }

  // Look through the free list until we find the position for the block,
  // then unlink it.
  while (IsFreeBlock(next, order)) {
    auto* cur = GetBlock(next);
    next = FreeBlockFields::NextFreeBlock::Get<size_t>(cur->header);
    if (next == block) {
      FreeBlockFields::NextFreeBlock::Set(
          &cur->header, FreeBlockFields::NextFreeBlock::Get<size_t>(to_remove->header));
      return true;
    }
  }

  return false;
}

zx_status_t Heap::Extend(size_t new_size) {
  if (cur_size_ == max_size_ && new_size > max_size_) {
    return ZX_ERR_NO_MEMORY;
  }
  new_size = std::min(max_size_, new_size);

  if (cur_size_ >= new_size) {
    return ZX_OK;
  }

  size_t min_index = IndexForOffset(cur_size_);
  size_t last_index = free_blocks_[kNumOrders - 1];
  // Ensure we start on an index at a page boundary.
  // Convert each new max order block to a free block in descending
  // order on the free list.
  size_t cur_index = IndexForOffset(new_size - new_size % kMinVmoSize);
  do {
    cur_index -= IndexForOffset(kMaxOrderSize);
    auto* block = GetBlock(cur_index);
    block->header = BlockFields::Order::Make(kNumOrders - 1) |
                    BlockFields::Type::Make(BlockType::kFree) |
                    FreeBlockFields::NextFreeBlock::Make(last_index);
    last_index = cur_index;
  } while (cur_index > min_index);

  free_blocks_[kNumOrders - 1] = last_index;

  cur_size_ = new_size;
  return ZX_OK;
}

}  // namespace internal
}  // namespace inspect
