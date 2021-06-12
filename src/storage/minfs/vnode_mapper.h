// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_VNODE_MAPPER_H_
#define SRC_STORAGE_MINFS_VNODE_MAPPER_H_

#include <range/range.h>

#include "src/storage/minfs/buffer_view.h"
#include "src/storage/minfs/lazy_reader.h"
#include "src/storage/minfs/pending_work.h"

namespace minfs {

class VnodeIterator;
class VnodeMinfs;

// Used to represent ranges of block pointers that can be in dnum, inum, dinum fields within the
// inode (corresponding to the direct, indirect and double indirect block pointers) or the pointers
// within the virtual indirect file.
class BlockPointerRange : public range::Range<uint64_t> {
 public:
  using range::Range<uint64_t>::Range;
};

// Maps from file to device blocks for the virtual indirect block file, which contains the leaf
// indirect blocks pointers, double indirect block pointers and leaf double indirect pointers. See
// the comment in the implementation file for a more detailed explanation.
class VnodeIndirectMapper : public MapperInterface {
 public:
  explicit VnodeIndirectMapper(VnodeMinfs* vnode) : vnode_(*vnode) {}

  [[nodiscard]] zx::status<DeviceBlockRange> Map(BlockRange range) override;

  [[nodiscard]] zx::status<DeviceBlockRange> MapForWrite(PendingWork* transaction, BlockRange range,
                                                         bool* allocated) override;

 private:
  // Returns a view into the indirect file for the blocks in |range|.
  zx::status<BufferView<blk_t>> GetView(PendingWork* transaction, BlockRange range);

  VnodeMinfs& vnode_;
};

// A mapper for a Minfs vnode, responsible for mapping from file blocks to device blocks.
class VnodeMapper : public MapperInterface {
 public:
  static constexpr uint64_t kIndirectFileStartBlock = kMinfsDirect;
  static constexpr uint64_t kDoubleIndirectFileStartBlock =
      kMinfsDirect + kMinfsDirectPerIndirect * kMinfsIndirect;
  static constexpr uint64_t kMaxBlocks =
      kDoubleIndirectFileStartBlock + kMinfsDirectPerDindirect * kMinfsDoublyIndirect;

  explicit VnodeMapper(VnodeMinfs* vnode) : vnode_(*vnode) {}

  VnodeMinfs& vnode() { return vnode_; }

  // MapperInterface:

  zx::status<DeviceBlockRange> Map(BlockRange range) override;

  zx::status<DeviceBlockRange> MapForWrite(PendingWork* transaction, BlockRange file_range,
                                           bool* allocated) override {
    // All allocations for Minfs vnodes are done elsewhere.
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // A convenience function that does the same as Map but returns a blk_t.
  [[nodiscard]] zx::status<std::pair<blk_t, uint64_t>> MapToBlk(BlockRange range);

 private:
  VnodeMinfs& vnode_;
};

// Iterator that keeps track of block pointers for a given file block. Depending on the file
// block, there can be up to three levels of block pointers.
//
// Example use, reading a range of blocks:
//
//   VnodeMapper mapper(vnode);
//   VnodeIterator iterator;
//   zx_status_t status = iterator.Init(&mapper, /*transaction=*/nullptr, start_block);
//   if (status != ZX_OK)
//     return status;
//   while (block_count > 0) {
//     blk_t block = iterator.Blk();
//     uint64_t count = iterator.GetContiguousBlockCount(block_count);
//     if (block) {
//       status = ReadBlocks(buffer, iterator.file_block(), block, count);
//       if (status != ZX_OK)
//         return status;
//     } else {
//       ZeroBlocks(buffer, iterator.file_block(), count);
//     }
//     status = iterator.Advance(count);
//     if (status != ZX_OK)
//       return status;
//     block_count -= count;
//   }
class VnodeIterator {
 public:
  // Users must call Init before the iterator is usable. Behaviour is undefined if any methods,
  // except the destructor, are called before Init has successfully returned.
  VnodeIterator() = default;

  // Movable but not copyable.
  VnodeIterator(VnodeIterator&&) = default;
  VnodeIterator& operator=(VnodeIterator&&) = default;

  // Initialize the iterator so that it is pointing at file_block. |transaction| can be nullptr in
  // which case the returned iterator is read-only. The iterator is left in an undefined state if
  // Init fails (except that it is safe to destroy).
  [[nodiscard]] zx_status_t Init(VnodeMapper* mapper, PendingWork* transaction,
                                 uint64_t file_block);

  // Returns the file block that the iterator is currently located at.
  uint64_t file_block() const { return file_block_; }

  // Returns the target block as a blk_t. Zero is special and means the block is unmapped/sparse.
  blk_t Blk() const {
    return level_count_ > 0 && levels_[0].remaining() > 0 ? levels_[0].blk() : 0;
  }

  // Sets the target block. The iterator will need to be flushed after calling this (by calling the
  // Flush method).
  [[nodiscard]] zx_status_t SetBlk(blk_t block) { return SetBlk(levels_.data(), block); }

  // Returns the length in blocks of a contiguous range at most |max_blocks|. For
  // efficiency/simplicity reasons, it might return fewer than there actually are.
  uint64_t GetContiguousBlockCount(
      uint64_t max_blocks = std::numeric_limits<uint64_t>::max()) const;

  // Flushes any changes that may have been made. This is a no-op if there are no changes or
  // this iterator is read-only.
  [[nodiscard]] zx_status_t Flush();

  // Advances the iterator by |advance| blocks. This will also flush the iterator first if
  // necessary.
  [[nodiscard]] zx_status_t Advance(uint64_t advance = 1);

 private:
  using ViewGetter = fit::function<zx::status<BufferView<blk_t>>(PendingWork*, VnodeMinfs* vnode,
                                                                 BlockPointerRange)>;

  // Level contains all the information required to manage block pointers at one particular
  // level. The iterator might need up to three levels of pointers to describe a particular
  // location. For example, if the block is in the double indirect region of the file, there will be
  // a pointer in the inode which points to an indirect block which contains another pointer to
  // another indirect block which has the pointer to the data block. Level holds a view to the bank
  // of pointers for each level.
  struct Level {
    // The number of remaining block pointers for this level.
    size_t remaining() const { return count - index; }
    // The target block as a blk_t.
    blk_t blk() const { return view.IsValid() ? view[index] : 0; }
    // This level could be sparse which means that there is no block allocated at the parent
    // level e.g. this level is for the leaf indirect block pointers and inum[indirect_index] == 0.
    bool IsSparse() const { return !view.IsValid(); }

    // A view to the block pointers for this level.
    BufferView<blk_t> view;
    // The current index on this level.
    size_t index = 0;
    // The number of pointers at this level.
    size_t count = 0;
    // The range of block pointers the view covers. These blocks are relative to the bank of
    // pointers, either the dnum, inum or dinum pointers, or the pointers in the virtual indirect
    // file.
    BlockPointerRange range = {0, 0};
    // A callback to get a view for this level to be used if necessary.
    ViewGetter view_getter;
  };

  // Worst case: double indirect (in inode) -> indirect -> indirect. See the comment at the top of
  // the implementation file for more detail.
  static constexpr int kMaxLevels = 3;

  zx_status_t InitializeLevel(int level, BlockPointerRange range, uint64_t block,
                              ViewGetter view_getter);
  zx_status_t InitializeIndirectLevel(int level, uint64_t relative_block);

  // Flushes the given level if there are any changes.
  [[nodiscard]] zx_status_t FlushLevel(int level);

  // Finds a contiguous run of blocks, but not necessarily the longest.
  uint64_t ComputeContiguousBlockCount() const;

  // Sets a block pointer in the given level.
  zx_status_t SetBlk(Level* level, blk_t block);

  // The owning mapper.
  VnodeMapper* mapper_ = nullptr;
  // A transaction to be used for allocations, or nullptr if read-only.
  PendingWork* transaction_ = nullptr;
  // The current file block that the iterator is pointing at.
  uint64_t file_block_ = 0;
  // The cached contiguous length returned by GetContiguousBlockCount().
  mutable uint64_t contiguous_block_count_ = 0;
  // The number of levels this iterator currently has.
  int level_count_ = 0;
  // The level information.
  std::array<Level, kMaxLevels> levels_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_VNODE_MAPPER_H_
