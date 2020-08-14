// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vnode_mapper.h"

#include <fs/trace.h>

#include "lazy_buffer.h"
#include "minfs_private.h"
#include "vnode.h"

namespace minfs {

// A Minfs file looks like:
//
// +-----------------------|-------------------------]------------------------+
// |     direct blocks     |    indirect blocks      | double indirect blocks |
// +-----------------------|-------------------------|------------------------+
// |<--kMinfsDirect (16)-->^<-------- [1] ---------->^<-------- [2] --------->^
//                         |                         |                        |
//                         |                         |                        |
//            kIndirectFileStartBlock                |                        |
//                                  kDoubleIndirectFileStartBlock             |
//                                                                        kMaxBlocks
//
// [1]: kMinfsIndirect (31) * kDirectPerIndirect (2048) = 63488
// [2]: kMinfsDoubleIndirect (1) * kDirectPerIndirect (2048) * kDirectPerIndirect (2048) = 4194304
//
// The pointers to those blocks are arranged like this:
//
//      inode.dnum             inode.inum              inode.dinum
//          |                      |                        |
//          v                      v                        v
//      Data blocks         Indirect blocks      Double indirect blocks
//                                 | (a)                    | (b)
//                                 v                        v
//                           Data blocks             Indirect blocks
//                                                          | (c)
//                                                          v
//                                                     Data blocks
//
// We support up to three *levels* of indirection.
//
// The pointers that aren't stored in an inode are stored in blocks that we need to allocate, and
// are referred to as indirect blocks. These blocks store 2048 block pointers. These blocks of block
// pointers are cached and stored in a VMO backed buffer, also known as the virtual indirect file,
// and it has the following layout:
//
//   +------------------------+----------------------------+--------------------------+
//   |    indirect-leaf (a)   |    double-indirect (b)     | double-indirect-leaf (c) |
//   +------------------------+----------------------------+--------------------------+
//   |<-kMinfsIndirect (31)-->|<-kMinfsDoublyIndirect (1)->|<--------- [1] ---------->|
//
// [1]: kMinfsDoublyIndirect (1) * kMinfsDirectPerIndirect (2048)

namespace {

// These constants are the offsets in terms of block pointers to (b) and (c) above, respectively.
constexpr uint64_t kDoubleIndirectViewStart = kMinfsIndirect * kMinfsDirectPerIndirect;
constexpr uint64_t kDoubleIndirectLeafViewStart =
    kDoubleIndirectViewStart + kMinfsDoublyIndirect * kMinfsDirectPerIndirect;

// Scans |array| for a contiguous range of blocks of at most |max_blocks|.
uint64_t Coalesce(const blk_t* array, uint64_t max_blocks) {
  uint64_t count = 1;
  if (*array == 0) {
    // A sparse range.
    while (count < max_blocks && array[count] == 0) {
      ++count;
    }
  } else {
    while (count < max_blocks && array[count] == array[count - 1] + 1) {
      ++count;
    }
  }
  return count;
}

// Converts blk_t to DeviceBlock. blk_t represents zeroed/sparse/unmapped blocks differently and are
// relative to |dat_block| in the super-block.
DeviceBlock ToDeviceBlock(VnodeMinfs* vnode, blk_t block) {
  return block == 0 ? DeviceBlock::Unmapped() : DeviceBlock(block + vnode->Vfs()->Info().dat_block);
}

// Returns a flusher responsible for flushing updated block pointers in the inode.
BaseBufferView::Flusher GetDirectFlusher(VnodeMinfs* vnode, PendingWork* transaction) {
  return [vnode, transaction](BaseBufferView* view) {
    vnode->InodeSync(transaction, kMxFsSyncDefault);
    return ZX_OK;
  };
}

// Returns a flusher responsible for flushing updated block pointers in indirect blocks;
BaseBufferView::Flusher GetIndirectFlusher(VnodeMinfs* vnode, LazyBuffer* file,
                                           PendingWork* transaction) {
#ifdef __Fuchsia__
  return [vnode, file, transaction](BaseBufferView* view) {
    VnodeIndirectMapper mapper(vnode);
    return file->Flush(
        transaction, &mapper, view,
        [transaction](ResizeableBufferType* buffer, BlockRange range, DeviceBlock device_block) {
          transaction->EnqueueMetadata(
              storage::Operation{
                  .type = storage::OperationType::kWrite,
                  .vmo_offset = range.Start(),
                  .dev_offset = device_block.block(),
                  .length = range.Length(),
              },
              buffer);
          return ZX_OK;
        });
  };
#else
  // TODO(fxbug.dev/47947): For the time being, host side code must immediately write data to the
  // device, but we should be able to make this code the same as above if storage::BufferedOperation
  // changes a bit (so that a pointer to the buffer is kept rather than a pointer to the address in
  // memory).
  return [vnode, file, transaction](BaseBufferView* view) {
    VnodeIndirectMapper mapper(vnode);
    return file->Flush(
        transaction, &mapper, view,
        [vnode](ResizeableBufferType* buffer, BlockRange range, DeviceBlock device_block) {
          return EnumerateBlocks(
              range, [vnode, buffer, range, device_block](BlockRange r) -> zx::status<uint64_t> {
                zx_status_t status = vnode->Vfs()->GetMutableBcache()->Writeblk(
                    device_block.block() + (r.Start() - range.Start()), buffer->Data(r.Start()));
                if (status == ZX_OK) {
                  return zx::ok(1);
                } else {
                  return zx::error(status);
                }
              });
        });
  };
#endif
}

// -- View Getters --
//
// These functions are helpers that set up BufferView objects for ranges of block pointers.

// The dnum block pointers.
zx::status<BufferView<blk_t>> GetInodeDirectView(PendingWork* transaction, VnodeMinfs* vnode,
                                                 BlockPointerRange range) {
  ZX_ASSERT(range.End() <= kMinfsDirect);
  return zx::ok(BufferView<blk_t>(BufferPtr::FromMemory(&vnode->GetMutableInode()->dnum),
                                  range.Start(), range.End() - range.Start(),
                                  transaction ? GetDirectFlusher(vnode, transaction) : nullptr));
}

// The inum block pointers.
zx::status<BufferView<blk_t>> GetInodeIndirectView(PendingWork* transaction, VnodeMinfs* vnode,
                                                   BlockPointerRange range) {
  ZX_ASSERT(range.End() <= kMinfsIndirect);
  return zx::ok(BufferView<blk_t>(BufferPtr::FromMemory(&vnode->GetMutableInode()->inum),
                                  range.Start(), range.End() - range.Start(),
                                  transaction ? GetDirectFlusher(vnode, transaction) : nullptr));
}

// The dinum block pointers.
zx::status<BufferView<blk_t>> GetInodeDoubleIndirectView(PendingWork* transaction,
                                                         VnodeMinfs* vnode,
                                                         BlockPointerRange range) {
  ZX_ASSERT(range.End() <= kMinfsDoublyIndirect);
  return zx::ok(BufferView<blk_t>(BufferPtr::FromMemory(&vnode->GetMutableInode()->dinum),
                                  range.Start(), range.End() - range.Start(),
                                  transaction ? GetDirectFlusher(vnode, transaction) : nullptr));
}

// The pointers in the indirect file. See diagram above to understand how these are laid out.
zx::status<BufferView<blk_t>> GetViewForIndirectFile(PendingWork* transaction, VnodeMinfs* vnode,
                                                     BlockPointerRange range) {
  zx::status<LazyBuffer*> file = vnode->GetIndirectFile();
  if (file.is_error())
    return file.take_error();
  VnodeIndirectMapper mapper(vnode);
  LazyBuffer::Reader reader(vnode->Vfs()->GetMutableBcache(), &mapper, file.value());
  return file.value()->GetView<blk_t>(
      range.Start(), range.End() - range.Start(), &reader,
      transaction ? GetIndirectFlusher(vnode, file.value(), transaction) : nullptr);
}

}  // namespace

// -- VnodeIndirectMapper --

zx::status<DeviceBlockRange> VnodeIndirectMapper::Map(BlockRange range) {
  return MapForWrite(/*transaction=*/nullptr, range, /*allocated=*/nullptr);
}

zx::status<BufferView<blk_t>> VnodeIndirectMapper::GetView(PendingWork* transaction,
                                                           BlockRange range) {
  constexpr uint64_t kDoubleIndirectLeafStart = kMinfsIndirect + kMinfsDoublyIndirect;
  constexpr uint64_t kMax =
      kDoubleIndirectLeafStart + kMinfsDoublyIndirect * kMinfsDirectPerIndirect;
  if (range.Start() < kMinfsIndirect) {
    // Region (a) -- Pointers are to be found in the inum field in the inode.
    return GetInodeIndirectView(
        transaction, &vnode_,
        BlockPointerRange(range.Start(),
                          std::min(range.End(), static_cast<uint64_t>(kMinfsIndirect))));
  } else if (range.Start() < kDoubleIndirectLeafStart) {
    // Region (b) -- Pointers are to be found in the dinum field in the inode.
    return GetInodeDoubleIndirectView(
        transaction, &vnode_,
        BlockPointerRange(range.Start() - kMinfsIndirect,
                          std::min(range.End(), kDoubleIndirectLeafStart) - kMinfsIndirect));
  } else if (range.Start() < kMax) {
    // Region (c) -- Pointers are to be found in region (b) of the virtual indirect file.
    return GetViewForIndirectFile(
        transaction, &vnode_,
        BlockPointerRange(
            range.Start() - kDoubleIndirectLeafStart + kDoubleIndirectViewStart,
            std::min(range.End(), kMax) - kDoubleIndirectLeafStart + kDoubleIndirectViewStart));
  } else {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
}

zx::status<DeviceBlockRange> VnodeIndirectMapper::MapForWrite(PendingWork* transaction,
                                                              BlockRange range, bool* allocated) {
  BufferView<blk_t> view;
  if (auto view_or = GetView(transaction, range); view_or.is_error()) {
    return view_or.take_error();
  } else {
    view = std::move(view_or).value();
  }
  // If |allocated| is non-null, allocate an indirect block if necessary.
  uint64_t block_count;
  if (*view == 0 && allocated) {
    block_count = 1;
    blk_t new_block = 0;
    vnode_.AllocateIndirect(transaction, &new_block);
    ZX_ASSERT(new_block != 0);
    view.mut_ref() = new_block;
    *allocated = true;
    zx_status_t status = view.Flush();
    if (status != ZX_OK)
      return zx::error(status);
  } else {
    if (allocated)
      *allocated = false;
    block_count = Coalesce(&view[0], view.count());
  }
  return zx::ok(DeviceBlockRange(ToDeviceBlock(&vnode_, *view), block_count));
}

// -- VnodeMapper --

zx::status<std::pair<blk_t, uint64_t>> VnodeMapper::MapToBlk(BlockRange range) {
  VnodeIterator iterator;
  zx_status_t status = iterator.Init(this, nullptr, range.Start());
  if (status != ZX_OK)
    return zx::error(status);
  return zx::ok(std::make_pair(iterator.Blk(), iterator.GetContiguousBlockCount(range.Length())));
}

zx::status<DeviceBlockRange> VnodeMapper::Map(BlockRange range) {
  zx::status<std::pair<blk_t, uint64_t>> status = MapToBlk(range);
  if (status.is_error())
    return status.take_error();
  return zx::ok(
      DeviceBlockRange(ToDeviceBlock(&vnode_, status.value().first), status.value().second));
}

// -- VnodeIterator --

zx_status_t VnodeIterator::Init(VnodeMapper* mapper, PendingWork* transaction,
                                uint64_t file_block) {
  mapper_ = mapper;
  transaction_ = transaction;
  file_block_ = file_block;
  contiguous_block_count_ = 0;
  // The file block determines the number of levels of views that we need, and the view-getters
  // that we need to use.
  if (file_block < VnodeMapper::kIndirectFileStartBlock) {
    // We only need the dnum pointers.
    level_count_ = 1;
    zx_status_t status =
        InitializeLevel(0, BlockPointerRange(0, kMinfsDirect), file_block, &GetInodeDirectView);
    if (status != ZX_OK)
      return status;
  } else if (file_block < VnodeMapper::kDoubleIndirectFileStartBlock) {
    // We need the inum pointers and the blocks they point to.
    level_count_ = 2;
    uint64_t relative_block = file_block - VnodeMapper::kIndirectFileStartBlock;
    zx_status_t status =
        InitializeLevel(1, BlockPointerRange(0, kMinfsIndirect),
                        relative_block / kMinfsDirectPerIndirect, &GetInodeIndirectView);
    if (status != ZX_OK)
      return status;
    status = InitializeIndirectLevel(0, relative_block);
    if (status != ZX_OK)
      return status;
  } else if (file_block < VnodeMapper::kMaxBlocks) {
    // We need the dinum pointers and two more levels.
    level_count_ = 3;
    uint64_t relative_block = file_block - VnodeMapper::kDoubleIndirectFileStartBlock;
    zx_status_t status =
        InitializeLevel(2, BlockPointerRange(0, kMinfsDoublyIndirect),
                        relative_block / kMinfsDirectPerIndirect / kMinfsDirectPerIndirect,
                        &GetInodeDoubleIndirectView);
    if (status != ZX_OK)
      return status;
    status = InitializeIndirectLevel(
        1, relative_block / kMinfsDirectPerIndirect + kDoubleIndirectViewStart);
    if (status != ZX_OK)
      return status;
    status = InitializeIndirectLevel(0, relative_block + kDoubleIndirectLeafViewStart);
    if (status != ZX_OK)
      return status;
  } else if (file_block == VnodeMapper::kMaxBlocks) {
    // Allow the iterator to point at the end.
    level_count_ = 0;
  } else {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_OK;
}

// Here |range| and |block| are blocks relative to the base of the pointers we are looking at, which
// could be the dnum, inum, dinum pointers or the pointers in the virtual indirect file. |block|
// must be contained within |range|.
zx_status_t VnodeIterator::InitializeLevel(int level, BlockPointerRange range, uint64_t block,
                                           ViewGetter view_getter) {
  ZX_ASSERT(block >= range.Start() && block < range.End());
  levels_[level].range = range;
  levels_[level].count = range.End() - range.Start();
  levels_[level].index = block - range.Start();
  // If the parent level is sparse, delay getting the view until we need to.
  if (level + 1 < level_count_ && levels_[level + 1].blk() == 0) {
    levels_[level].view = {};
    levels_[level].view_getter = std::move(view_getter);
  } else {
    zx::status<BufferView<blk_t>> view = view_getter(transaction_, &mapper_->vnode(), range);
    if (view.is_error())
      return view.status_value();
    levels_[level].view = std::move(view).value();
    levels_[level].view_getter = nullptr;
  }
  return ZX_OK;
}

// Convenience function for levels that point to the virtual indirect file. Here |relative_block| is
// the pointer offset within the virtual indirect file.
zx_status_t VnodeIterator::InitializeIndirectLevel(int level, uint64_t relative_block) {
  uint64_t first_block = fbl::round_down(relative_block, kMinfsDirectPerIndirect);
  return InitializeLevel(level,
                         BlockPointerRange(first_block, first_block + kMinfsDirectPerIndirect),
                         relative_block, &GetViewForIndirectFile);
}

zx_status_t VnodeIterator::SetBlk(Level* level, blk_t block) {
  ZX_ASSERT(level_count_ > 0);
  // If this level is sparse, try and get a view for it now.
  if (level->IsSparse()) {
    if (block == 0)
      return ZX_OK;
    zx::status<BufferView<blk_t>> view =
        level->view_getter(transaction_, &mapper_->vnode(), level->range);
    if (view.is_error())
      return view.status_value();
    level->view = std::move(view).value();
  }
  level->view.mut_ref(level->index) = block;
  return ZX_OK;
}

uint64_t VnodeIterator::GetContiguousBlockCount(uint64_t max_blocks) const {
  if (level_count_ == 0)
    return 0;
  if (contiguous_block_count_ == 0)
    contiguous_block_count_ = ComputeContiguousBlockCount();
  return std::min(contiguous_block_count_, max_blocks);
}

uint64_t VnodeIterator::ComputeContiguousBlockCount() const {
  // For efficiency reasons, handle sparse ranges differently. This is so we can quickly scan the
  // (typically) unallocated/sparse blocks from the end of the file.
  if (Blk() == 0) {
    // The number of blocks we have found so far.
    uint64_t count = 0;
    // The number of blocks a block pointer represents at the current level.
    uint64_t multiplier = 1;
    // The index into the view for the current level.
    uint64_t index = levels_[0].index;
    int level = 0;
    // N.B. When we truncate blocks, we rely on the fact that we only go *up* the tree here, *not*
    // down. To further explain, consider the case where the inode points to an indirect block, but
    // the indirect block doesn't happen to have any blocks allocated. We could, in theory, coalesce
    // those blocks and just say it's all sparse, but if we did that, we wouldn't free the indirect
    // block. Instead, we'll coalesce as many blocks as we can at the lowest level, then move up a
    // level and coalesce all the blocks at that level, but we'll stop as soon as we find an
    // allocated block, even though that indirect block might not point to any allocated blocks.
    for (;;) {
      const uint64_t left = levels_[level].count - index;
      if (left == 0 || levels_[level].IsSparse()) {
        count += left * multiplier;
      } else if (levels_[level].view[index] == 0) {
        uint64_t contiguous = Coalesce(&levels_[level].view[index], left);
        count += contiguous * multiplier;
        if (contiguous < left)
          return count;
      } else {
        // We've come to a block that isn't sparse.
        return count;
      }
      if (++level >= level_count_)
        return count;
      multiplier *= kMinfsDirectPerIndirect;
      index = levels_[level].index + 1;
    }
  } else {
    return Coalesce(&levels_[0].view[levels_[0].index], levels_[0].remaining());
  }
}

zx_status_t VnodeIterator::FlushLevel(int level) {
  if (!transaction_)
    return ZX_OK;
  if (level + 1 < level_count_) {
    const blk_t parent_block = levels_[level + 1].blk();
    // If this block is now empty and we have a parent, deallocate rather than writing block. As an
    // optimisation, we quickly check that item current pointed at is zero before doing a full check
    // of the whole block.
    if (parent_block != 0 && levels_[level].blk() == 0) {
      ZX_ASSERT(levels_[level].view.count() == kMinfsDirectPerIndirect);
      bool empty = true;
      for (unsigned i = 0; i < kMinfsDirectPerIndirect; ++i) {
        if (levels_[level].view[i] != 0) {
          empty = false;
          break;
        }
      }
      if (empty) {
        // Delete the block and update the parent.
        mapper_->vnode().DeleteBlock(transaction_, 0, parent_block, /*indirect=*/true);
        SetBlk(&levels_[level + 1], 0);
        levels_[level].view.set_dirty(false);
        return ZX_OK;
      }
    }
    // If there are modifications and the parent doesn't have a block, allocate it now. This isn't
    // strictly necessary because VnodeIndirectMapper will allocate if it needs to. However, it will
    // immediately flush whereas if we do it here, we can delay the flush as there might be more
    // changes to make later.
    if (levels_[level].view.dirty() && parent_block == 0) {
      blk_t new_block;
      mapper_->vnode().AllocateIndirect(transaction_, &new_block);
      ZX_ASSERT(new_block != 0);
      SetBlk(&levels_[level + 1], new_block);
    }
  }
  return levels_[level].view.Flush();
}

zx_status_t VnodeIterator::Flush() {
  if (!transaction_)
    return ZX_OK;  // Iterator is read-only.
  for (int level = 0; level < level_count_; ++level) {
    zx_status_t status = FlushLevel(level);
    if (status != ZX_OK)
      return status;
  }
  return ZX_OK;
}

zx_status_t VnodeIterator::Advance(const uint64_t advance) {
  if (level_count_ == 0) {
    return advance == 0 ? ZX_OK : ZX_ERR_BAD_STATE;
  }
  // Short circuit for the common case.
  if (advance < levels_[0].remaining()) {
    levels_[0].index += advance;
    file_block_ += advance;
    if (advance >= contiguous_block_count_) {
      contiguous_block_count_ = 0;
    } else {
      contiguous_block_count_ -= advance;
    }
    return ZX_OK;
  }
  // Get a new iterator for the new file block.
  VnodeIterator iterator;
  zx_status_t status = iterator.Init(mapper_, transaction_, file_block_ + advance);
  if (status != ZX_OK)
    return status;
  // Now see which of the old views need flushing.
  if (iterator.level_count_ != level_count_) {
    // The level count is different so we flush all of the levels.
    status = Flush();
    if (status != ZX_OK)
      return status;
  } else {
    // If the two iterators have the same view for a level, just move the view. This prevents us
    // from over-flushing.
    for (int level = 0; level < level_count_; ++level) {
      if (levels_[level].range == iterator.levels_[level].range) {
        // The ranges are the same and because the level count is the same, we know that the view
        // must point to the same thing, so we can just move the views over and defer flushing this
        // level.
        iterator.levels_[level].view = std::move(levels_[level].view);
      } else {
        status = FlushLevel(level);
        if (status != ZX_OK)
          return status;
      }
    }
  }
  *this = std::move(iterator);
  return ZX_OK;
}

}  // namespace minfs
