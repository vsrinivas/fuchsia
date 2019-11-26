// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_VNODE_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_VNODE_H_

#include <inttypes.h>

#include <memory>
#include <utility>

#ifdef __Fuchsia__
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/minfs/c/fidl.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <fbl/auto_lock.h>
#include <fs/remote.h>
#include <fs/watcher.h>

#include "vnode-allocation.h"
#endif

#include <lib/zircon-internal/fnv1hash.h>

#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fs/locking.h>
#include <fs/ticker.h>
#include <fs/trace.h>
#include <fs/transaction/block_transaction.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <minfs/format.h>
#include <minfs/minfs.h>
#include <minfs/transaction-limits.h>
#include <minfs/writeback.h>

namespace minfs {

// Used by fsck
class MinfsChecker;
class Minfs;

// An abstract Vnode class contains the following:
//
// - A VMO, holding the in-memory representation of data stored persistently.
// - An inode, holding the root of this node's metadata.
//
// This class is capable of writing, reading, and truncating the node's data
// in a linear block-address space.
class VnodeMinfs : public fs::Vnode,
                   public fbl::SinglyLinkedListable<VnodeMinfs*>,
                   public fbl::Recyclable<VnodeMinfs> {
 public:
  virtual ~VnodeMinfs();

  // Allocates a new Vnode and initializes the in-memory inode structure given the type, where
  // type is one of:
  // - kMinfsTypeFile
  // - kMinfsTypeDir
  //
  // Sets create / modify times of the new node.
  // Does not allocate an inode number for the Vnode.
  static void Allocate(Minfs* fs, uint32_t type, fbl::RefPtr<VnodeMinfs>* out);

  // Allocates a Vnode, loading |ino| from storage.
  //
  // Doesn't update create / modify times of the node.
  static void Recreate(Minfs* fs, ino_t ino, fbl::RefPtr<VnodeMinfs>* out);

  bool IsUnlinked() const { return inode_.link_count == 0; }

  const Inode* GetInode() const { return &inode_; }
  ino_t GetIno() const { return ino_; }

  ino_t GetKey() const { return ino_; }
  // Should only be called once for the VnodeMinfs lifecycle.
  void SetIno(ino_t ino);

  void SetNextInode(ino_t ino) { inode_.next_inode = ino; }
  void SetLastInode(ino_t ino) { inode_.last_inode = ino; }

  void AddLink() { inode_.link_count++; }

  static size_t GetHash(ino_t key) { return fnv1a_tiny(key, kMinfsHashBits); }

  // fs::Vnode interface (invoked publicly).
#ifdef __Fuchsia__
  zx_status_t HandleFsSpecificMessage(fidl_msg_t* msg, fidl_txn_t* txn) final;
  static const fuchsia_minfs_Minfs_ops* Ops();
#endif
  using fs::Vnode::Open;
  zx_status_t Open(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect) final;
  zx_status_t Close() final;

  // fbl::Recyclable interface.
  void fbl_recycle() override;

  // Queries the underlying vnode to ask if it may be unlinked.
  //
  // If the response is not ZX_OK, operations to unlink (or rename on top of) this
  // vnode will fail.
  virtual zx_status_t CanUnlink() const = 0;

  // Returns the current block count of the vnode.
  virtual blk_t GetBlockCount() const = 0;

  // Returns the total size of the vnode.
  virtual uint64_t GetSize() const = 0;

  // Returns if the node is a directory.
  // TODO(fxb/39864): This function is used only within minfs to implement unlinking and renaming.
  // Consider replacing this with the more general |Vnode::GetProtocols|.
  virtual bool IsDirectory() const = 0;

  // Sets the new size of the vnode.
  // Should update the in-memory representation of the Vnode, but not necessarily
  // write it out to persistent storage.
  //
  // TODO: Upgrade internal size to 64-bit integer.
  virtual void SetSize(uint32_t new_size) = 0;

  // Accesses a block in the vnode at |vmo_offset| relative to the start of the file,
  // which was previously at the device offset |dev_offset|.
  //
  // If the block was not previously allocated, |dev_offset| is zero.
  // |*out_dev_offset| must contain the new value of the device offset to use when writing
  // to this part of the Vnode. By default, it is set to |dev_offset|.
  //
  // |*out_dev_offset| may be passed to |IssueWriteback| as |dev_offset|.
  virtual void AcquireWritableBlock(Transaction* transaction, blk_t vmo_offset, blk_t dev_offset,
                                    blk_t* out_dev_offset) = 0;

  // Deletes the block at |vmo_offset| within the file, corresponding to on-disk
  // block |dev_offset| (zero if unallocated).
  virtual void DeleteBlock(PendingWork* transaction, blk_t vmo_offset, blk_t dev_offset) = 0;

#ifdef __Fuchsia__
  // Instructs the Vnode to write out |count| blocks of the vnode, starting at local
  // offset |vmo_offset|, corresponding to on-disk offset |dev_offset|.
  virtual void IssueWriteback(Transaction* transaction, blk_t vmo_offset, blk_t dev_offset,
                              blk_t count) = 0;

  // Queries the node, returning |true| if the node has an in-flight operation on |vmo_offset|
  // that has not yet been enqueued to the writeback pipeline.
  virtual bool HasPendingAllocation(blk_t vmo_offset) = 0;

  // Instructs the node to cancel all pending writeback operations that have not yet been
  // enqueued to the writeback pipeline.
  //
  // This method is used exclusively when deleting nodes.
  virtual void CancelPendingWriteback() = 0;

  // Minfs FIDL interface.
  zx_status_t GetMetrics(fidl_txn_t* transaction);
  zx_status_t ToggleMetrics(bool enabled, fidl_txn_t* transaction);
  zx_status_t GetAllocatedRegions(fidl_txn_t* transaction) const;

#endif
  Minfs* Vfs() { return fs_; }

  // Local implementations of read, write, and truncate functions which
  // may operate on either files or directories.
  zx_status_t ReadInternal(PendingWork* transaction, void* data, size_t len, size_t off,
                           size_t* actual);
  zx_status_t ReadExactInternal(PendingWork* transaction, void* data, size_t len, size_t off);
  zx_status_t WriteInternal(Transaction* transaction, const void* data, size_t len, size_t off,
                            size_t* actual);
  zx_status_t WriteExactInternal(Transaction* transaction, const void* data, size_t len,
                                 size_t off);
  zx_status_t TruncateInternal(Transaction* transaction, size_t len);

  // TODO: The following methods should be made private:
#ifdef __Fuchsia__
  zx_status_t InitIndirectVmo();
  // Reads the block at |offset| in memory.
  // Assumes that vmo_indirect_ has already been initialized
  void ReadIndirectVmoBlock(uint32_t offset, uint32_t** entry);
  // Loads indirect blocks up to and including the doubly indirect block at |index|.
  zx_status_t LoadIndirectWithinDoublyIndirect(uint32_t index);
#else
  // Reads the block at |bno| on disk.
  void ReadIndirectBlock(blk_t bno, uint32_t* entry);
#endif

  // Update the vnode's inode and write it to disk.
  void InodeSync(PendingWork* transaction, uint32_t flags);

  // Decrements the inode link count to a vnode.
  // Writes the inode back to |transaction|.
  //
  // If the link count becomes zero, the node either:
  // 1) Calls |Purge()| (if no open fds exist), or
  // 2) Adds itself to the "unlinked list", to be purged later.
  void RemoveInodeLink(PendingWork* transaction);

  // TODO(smklein): These operations and members are protected as a historical artifact
  // of "File + Directory + Vnode" being a single class. They should be transitioned to
  // private.
 protected:
  VnodeMinfs(Minfs* fs);

  // fs::Vnode interface.
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t SetAttributes(fs::VnodeAttributesUpdate a) final;
#ifdef __Fuchsia__
  zx_status_t QueryFilesystem(fuchsia_io_FilesystemInfo* out) final;
  zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) final;
#endif

  // Although file sizes don't need to be block-aligned, the underlying VMO is
  // always kept at a size which is a multiple of |kMinfsBlockSize|.
  //
  // When a Vnode is truncated to a size larger than |inode_.size|, it is
  // assumed that any space between |inode_.size| and the nearest block is
  // filled with zeroes in the internal VMO. This function validates that
  // assumption.
  void ValidateVmoTail(uint64_t inode_size) const;

  enum class BlockOp {
    // Read skips unallocated indirect blocks, setting all output |bno| values to zero.
    kRead,
    // Delete avoids accessing indirect blocks, but additionally releases indirect blocks
    // (and doubly indirect blocks) if all contained blocks have been freed.
    //
    // |out_dev_offset| must be zero for all callbacks invoked via this operation.
    kDelete,
    // Write ensures all indirect blocks are allocated before accessing the underlying |bno|.
    // Acquiring a block via "kWrite" may cause additional writeback traffic to update
    // the metadata itself.
    kWrite,
    // Swap is identical to write: It ensures all indirect blocks are allocated
    // before being accessed.
    kSwap,
  };

  // Callback for block operations. Called exclusively on "leaf node" blocks: indirect blocks
  // are considered metadata, and handled internally by the "BlockOp" functions.
  //
  // |vmo_offset|: Block address relative to start of Vnode.
  // |dev_offset|: Previous absolute block address at this node. Zero if unallocated.
  // |out_dev_offset|: A new, optional output value. Set to |dev_offset| by default.
  //            Will alter the results of |bno| returned via |ApplyOperation|.
  using BlockOpCallback =
      fbl::Function<void(blk_t vmo_offset, blk_t dev_offset, blk_t* out_dev_offset)>;

  // Arguments to invoke |callback| on all local nodes of the file in [start, start + count).
  //
  // Collects result blocks in |bnos|.
  struct BlockOpArgs {
    BlockOpArgs(Transaction* transaction, BlockOp op, BlockOpCallback callback, blk_t start,
                blk_t count, blk_t* bnos)
        : transaction(transaction),
          op(op),
          callback(std::move(callback)),
          start(start),
          count(count),
          bnos(bnos) {
      // Initialize output array to 0 in case the indirect block(s)
      // containing these bnos do not exist.
      if (bnos) {
        memset(bnos, 0, sizeof(blk_t) * count);
      }
    }

    Transaction* transaction;
    BlockOp op;
    BlockOpCallback callback;
    blk_t start;
    blk_t count;
    blk_t* bnos;
  };

  class DirectArgs {
   public:
    DirectArgs(BlockOp op, blk_t* array, blk_t count, blk_t rel_bno, blk_t* bnos)
        : array_(array), bnos_(bnos), count_(count), rel_bno_(rel_bno), op_(op), dirty_(false) {}

    BlockOp GetOp() const { return op_; }
    blk_t GetBno(blk_t index) const { return array_[index]; }
    void SetBno(blk_t index, blk_t value) {
      ZX_DEBUG_ASSERT(index < GetCount());

      if (bnos_ != nullptr) {
        bnos_[index] = value ? value : array_[index];
      }

      if (array_[index] != value) {
        array_[index] = value;
        dirty_ = true;
      }
    }

    blk_t GetCount() const { return count_; }
    blk_t GetRelativeBlock() const { return rel_bno_; }

    bool IsDirty() const { return dirty_; }

   protected:
    blk_t* const array_;   // array containing blocks to be operated on
    blk_t* const bnos_;    // array of |count| bnos returned to the user
    const blk_t count_;    // number of direct blocks to operate on
    const blk_t rel_bno_;  // The relative bno of the first direct block we are op'ing.
    const BlockOp op_;     // determines what operation to perform on blocks
    bool dirty_;           // true if blocks have successfully been op'd
  };

  class IndirectArgs : public DirectArgs {
   public:
    IndirectArgs(BlockOp op, blk_t* array, blk_t count, blk_t rel_bno, blk_t* bnos, blk_t bindex,
                 blk_t ib_vmo_offset)
        : DirectArgs(op, array, count, rel_bno, bnos),
          bindex_(bindex),
          ib_vmo_offset_(ib_vmo_offset) {}

    void SetDirty() { dirty_ = true; }

    void SetBno(blk_t index, blk_t value) {
      ZX_DEBUG_ASSERT(index < GetCount());
      array_[index] = value;
      SetDirty();
    }

    // Number of indirect blocks we need to iterate through to touch all |count| direct blocks.
    blk_t GetCount() const {
      return (bindex_ + count_ + kMinfsDirectPerIndirect - 1) / kMinfsDirectPerIndirect;
    }

    blk_t GetOffset() const { return ib_vmo_offset_; }

    // Generate parameters for direct blocks in indirect block |ibindex|, which are contained
    // in |barray|
    DirectArgs GetDirect(blk_t* barray, unsigned ibindex) const;

   protected:
    const blk_t bindex_;  // relative index of the first direct block within the first indirect
                          // block
    const blk_t ib_vmo_offset_;  // index of the first indirect block
  };

  class DindirectArgs : public IndirectArgs {
   public:
    DindirectArgs(BlockOp op, blk_t* array, blk_t count, blk_t rel_bno, blk_t* bnos, blk_t bindex,
                  blk_t ib_vmo_offset, blk_t ibindex, blk_t dib_vmo_offset)
        : IndirectArgs(op, array, count, rel_bno, bnos, bindex, ib_vmo_offset),
          ibindex_(ibindex),
          dib_vmo_offset_(dib_vmo_offset) {}

    // Number of doubly indirect blocks we need to iterate through to touch all |count| direct
    // blocks.
    blk_t GetCount() const {
      return (ibindex_ + count_ + kMinfsDirectPerDindirect - 1) / kMinfsDirectPerDindirect;
    }

    blk_t GetOffset() const { return dib_vmo_offset_; }

    // Generate parameters for indirect blocks in doubly indirect block |dibindex|, which are
    // contained in |iarray|
    IndirectArgs GetIndirect(blk_t* iarray, unsigned dibindex) const;

   protected:
    const blk_t ibindex_;         // relative index of the first indirect block within the first
                                  // doubly indirect block
    const blk_t dib_vmo_offset_;  // index of the first doubly indirect block
  };

  // Allocate an indirect or doubly indirect block at |offset| within the indirect vmo and clear
  // the in-memory block array
  // Assumes that vmo_indirect_ has already been initialized
  void AllocateIndirect(Transaction* transaction, blk_t index, IndirectArgs* args);

  // Perform operation |op| on blocks as specified by |params|
  // The BlockOp methods should not be called directly
  // All BlockOp methods assume that vmo_indirect_ has been grown to the required size
  zx_status_t ApplyOperation(BlockOpArgs* params);
  zx_status_t BlockOpDirect(BlockOpArgs* op_args, DirectArgs* params);
  zx_status_t BlockOpIndirect(BlockOpArgs* op_args, IndirectArgs* params);
  zx_status_t BlockOpDindirect(BlockOpArgs* op_args, DindirectArgs* params);

  // Ensures that the indirect vmo is large enough to reference a block at
  // relative block address |n| within the file.
  zx_status_t EnsureIndirectVmoSize(blk_t n);

  // Get the disk block 'bno' corresponding to the 'n' block
  //
  // May or may not allocate |bno|; certain Vnodes (like File) delay allocation
  // until writeback, and will return a sentinel value of zero.
  //
  // TODO: Use types to represent that |bno|, as an output, is optional.
  zx_status_t BlockGetWritable(Transaction* transaction, blk_t n, blk_t* bno);

  // Get the disk block 'bno' corresponding to relative block address |n| within the file.
  // Does not allocate any blocks, direct or indirect, to acquire this block.
  zx_status_t BlockGetReadable(blk_t n, blk_t* bno);

  // Deletes all blocks (relative to a file) from "start" (inclusive) to the end
  // of the file. Does not update mtime/atime.
  // This can be extended to return indices of deleted bnos, or to delete a specific number of
  // bnos
  zx_status_t BlocksShrink(Transaction* transaction, blk_t start);

  // Deletes this Vnode from disk, freeing the inode and blocks.
  //
  // Must only be called on Vnodes which
  // - Have no open fds
  // - Are fully unlinked (link count == 0)
  void Purge(PendingWork* transaction);

#ifdef __Fuchsia__
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final;

  void Sync(SyncCallback closure) final;
  zx_status_t AttachRemote(fs::MountChannel h) final;
  zx_status_t InitVmo(PendingWork* transaction);

  // Initializes the indirect VMO, grows it to |size| bytes, and reads |count| indirect
  // blocks from |iarray| into the indirect VMO, starting at block offset |offset|.
  zx_status_t LoadIndirectBlocks(blk_t* iarray, uint32_t count, uint32_t offset, uint64_t size);

  // Clears the block at |offset| in memory.
  // Assumes that vmo_indirect_ has already been initialized
  void ClearIndirectVmoBlock(uint32_t offset);

  // Use the watcher container to implement a directory watcher
  void Notify(fbl::StringPiece name, unsigned event) final;
  zx_status_t WatchDir(fs::Vfs* vfs, uint32_t mask, uint32_t options, zx::channel watcher) final;

#else  // !__Fuchsia__
  // Clears the block at |bno| on disk.
  void ClearIndirectBlock(blk_t bno);
#endif
  uint32_t FdCount() const { return fd_count_; }

  Minfs* const fs_;
#ifdef __Fuchsia__
  // TODO(smklein): When we have can register MinFS as a pager service, and
  // it can properly handle pages faults on a vnode's contents, then we can
  // avoid reading the entire file up-front. Until then, read the contents of
  // a VMO into memory when it is read/written.
  zx::vmo vmo_{};
  uint64_t vmo_size_ = 0;

  // vmo_indirect_ contains all indirect and doubly indirect blocks in the following order:
  // First kMinfsIndirect blocks                                - initial set of indirect blocks
  // Next kMinfsDoublyIndirect blocks                           - doubly indirect blocks
  // Next kMinfsDoublyIndirect * kMinfsDirectPerIndirect blocks - indirect blocks pointed to
  //                                                              by doubly indirect blocks
  // DataBlockAssigner may modify this field asynchronously, so a valid Transaction object must
  // be held before accessing it.
  //
  // vmo_indirect_ layout is sparse even when the corresponding file is not sparse.
  // Meaning, the layout of vmo looks something like
  // +----------------+-----------------+-----------------+------+-----------------+...
  // | indirect block | dindirect block | indirect blocks | hole | indirect blocks |...
  // +----------------+-----------------+-----------------+------+-----------------+...
  // Above, the "hole" in vmo address range will never contain valid data(block numbers)
  // irrespective of how large the file gets. This is because of how GetVmoOffsetForIndirect
  // is implemented. Having sparse vmo layout, without any need for it to be sparse,
  // makes reading/debugging difficult.
  // TODO(fxb/42096).
  std::unique_ptr<fzl::ResizeableVmoMapper> vmo_indirect_;

  fuchsia_hardware_block_VmoID vmoid_{};
  fuchsia_hardware_block_VmoID vmoid_indirect_{};

  fs::WatcherContainer watcher_{};
#endif

  ino_t ino_{};

  // DataBlockAssigner may modify this field asynchronously, so a valid Transaction object must
  // be held before accessing it.
  Inode inode_{};

  // This field tracks the current number of file descriptors with
  // an open reference to this Vnode. Notably, this is distinct from the
  // VnodeMinfs's own refcount, since there may still be filesystem
  // work to do after the last file descriptor has been closed.
  uint32_t fd_count_{};
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_VNODE_H_
