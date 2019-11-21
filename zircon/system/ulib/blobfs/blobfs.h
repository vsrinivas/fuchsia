// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the global Blobfs structure used for constructing a Blobfs filesystem in
// memory.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_BLOBFS_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_BLOBFS_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fuchsia/blobfs/llcpp/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <bitmap/raw-bitmap.h>
#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/mount.h>
#include <block-client/cpp/block-device.h>
#include <block-client/cpp/block-group-registry.h>
#include <block-client/cpp/client.h>
#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <fs/journal/journal.h>
#include <fs/trace.h>
#include <fs/transaction/block_transaction.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <storage/operation/unbuffered-operations-builder.h>
#include <trace/event.h>

#include "allocator/allocator.h"
#include "allocator/extent-reserver.h"
#include "allocator/node-reserver.h"
#include "blob-cache.h"
#include "directory.h"
#include "iterator/allocated-extent-iterator.h"
#include "iterator/extent-iterator.h"
#include "metrics.h"
#include "pager/user-pager.h"
#include "transaction-manager.h"

namespace blobfs {

using block_client::BlockDevice;
using digest::Digest;
using storage::OperationType;
using storage::UnbufferedOperationsBuilder;

constexpr char kOutgoingDataRoot[] = "root";

class Blobfs : public TransactionManager, public UserPager {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Blobfs);

  // Creates a blobfs object
  static zx_status_t Create(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device,
                            MountOptions* options, std::unique_ptr<Blobfs>* out);

  static std::unique_ptr<BlockDevice> Destroy(std::unique_ptr<Blobfs> blobfs);

  virtual ~Blobfs();

  ////////////////
  // TransactionManager's fs::TransactionHandler interface.
  //
  // Allows transmitting read and write transactions directly to the underlying storage.

  uint32_t FsBlockSize() const final { return kBlobfsBlockSize; }

  uint32_t DeviceBlockSize() const final { return block_info_.block_size; }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final {
    return block_num * kBlobfsBlockSize / block_info_.block_size;
  }

  zx_status_t RunOperation(const storage::Operation& operation, storage::BlockBuffer* buffer) final;

  groupid_t BlockGroupID() final;

  block_client::BlockDevice* GetDevice() final { return block_device_.get(); }

  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
    TRACE_DURATION("blobfs", "Blobfs::Transaction", "count", count);
    return block_device_->FifoTransaction(requests, count);
  }

  ////////////////
  // TransactionManager's SpaceManager interface.
  //
  // Allows viewing and controlling the size of the underlying volume.

  const Superblock& Info() const final { return info_; }
  zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final;
  zx_status_t DetachVmo(vmoid_t vmoid) final;
  zx_status_t AddInodes(fzl::ResizeableVmoMapper* node_map) final;
  zx_status_t AddBlocks(size_t nblocks, RawBitmap* block_map) final;

  ////////////////
  // TransactionManager interface.
  //
  // Allows attaching VMOs, controlling the underlying volume, and sending transactions to the
  // underlying storage (optionally through the journal).

  BlobfsMetrics& Metrics() final { return metrics_; }
  size_t WritebackCapacity() const final;
  fs::Journal* journal() final;
  Writability writability() const { return writability_; }

  ////////////////
  // Other methods.

  // Returns the internal blobfs dispatcher.
  async_dispatcher_t* dispatcher() { return dispatcher_; }

  bool CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                            uint64_t* first_unset = nullptr) const {
    return allocator_->CheckBlocksAllocated(start_block, end_block, first_unset);
  }

  NodeFinder* GetNodeFinder() { return allocator_.get(); }

  Allocator* GetAllocator() { return allocator_.get(); }

  Inode* GetNode(uint32_t node_index) { return allocator_->GetNode(node_index); }

  // Invokes "open" on the root directory.
  // Acts as a special-case to bootstrap filesystem mounting.
  zx_status_t OpenRootNode(fbl::RefPtr<fs::Vnode>* out, ServeLayout layout);

  BlobCache& Cache() { return blob_cache_; }

  zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual);

  BlockDevice* Device() const { return block_device_.get(); }

  // Returns an unique identifier for this instance.
  uint64_t GetFsId() const { return fs_id_; }

  using SyncCallback = fs::Vnode::SyncCallback;
  void Sync(SyncCallback closure);

  // Frees an inode, from both the reserved map and the inode table. If the
  // inode was allocated in the inode table, write the deleted inode out to
  // disk.
  void FreeInode(uint32_t node_index, storage::UnbufferedOperationsBuilder* operations,
                 fbl::Vector<storage::BufferedOperation>* trim_data);

  // Writes node data to the inode table and updates disk.
  void PersistNode(uint32_t node_index, storage::UnbufferedOperationsBuilder* operations);

  // Adds reserved blocks to allocated bitmap and writes the bitmap out to disk.
  void PersistBlocks(const ReservedExtent& extent, storage::UnbufferedOperationsBuilder* ops);

  bool PagingEnabled() const { return paging_enabled_; }

 private:
  friend class BlobfsChecker;

  ////////////////
  // UserPager interface.
  //
  // Allows populating the pager transfer buffer with a blob's blocks from the block device.
  zx_status_t AttachTransferVmo(const zx::vmo& transfer_vmo) final;
  zx_status_t PopulateTransferVmo(uint32_t map_index, uint32_t start_block,
                                  uint32_t block_count) final;

  Blobfs(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device,
         const Superblock* info, Writability writable);

  // Terminates all internal connections, updates the "clean bit" (if writable),
  // flushes writeback buffers, empties caches, and returns the underlying
  // block device.
  std::unique_ptr<BlockDevice> Reset();

  // Does a single pass of all blobs, creating uninitialized Vnode
  // objects for them all.
  //
  // By executing this function at mount, we can quickly assert
  // either the presence or absence of a blob on the system without
  // further scanning.
  zx_status_t InitializeVnodes();

  // Reloads metadata from disk. Useful when metadata on disk
  // may have changed due to journal playback.
  zx_status_t ReloadSuperblock();

  // Frees blocks from the allocated map (if allocated) and updates disk if necessary.
  void FreeExtent(const Extent& extent, storage::UnbufferedOperationsBuilder* operations,
                  fbl::Vector<storage::BufferedOperation>* trim_data);

  // Free a single node. Doesn't attempt to parse the type / traverse nodes;
  // this function just deletes a single node.
  void FreeNode(uint32_t node_index, storage::UnbufferedOperationsBuilder* operations);

  // Given a contiguous number of blocks after a starting block,
  // write out the bitmap to disk for the corresponding blocks.
  // Should only be called by PersistBlocks and FreeExtent.
  void WriteBitmap(uint64_t nblocks, uint64_t start_block,
                   storage::UnbufferedOperationsBuilder* operations);

  // Given a node within the node map at an index, write it to disk.
  // Should only be called by AllocateNode and FreeNode.
  void WriteNode(uint32_t map_index, storage::UnbufferedOperationsBuilder* operations);

  // Enqueues an update for allocated inode/block counts.
  void WriteInfo(storage::UnbufferedOperationsBuilder* operations);

  // Adds a trim operation to |trim_data|.
  void DeleteExtent(uint64_t start_block, uint64_t num_blocks,
                    fbl::Vector<storage::BufferedOperation>* trim_data);

  // Creates an unique identifier for this instance. This is to be called only during
  // "construction".
  zx_status_t CreateFsId();

  // Verifies that the contents of a blob are valid.
  zx_status_t VerifyBlob(uint32_t node_index);

  // Updates the flags field in superblock.
  void UpdateFlags(storage::UnbufferedOperationsBuilder* operations, uint32_t flags, bool set);

  std::unique_ptr<fs::Journal> journal_;
  Superblock info_;

  BlobCache blob_cache_;

  async_dispatcher_t* dispatcher_ = nullptr;
  std::unique_ptr<BlockDevice> block_device_;
  fuchsia_hardware_block_BlockInfo block_info_ = {};
  block_client::BlockGroupRegistry group_registry_;
  Writability writability_;

  std::unique_ptr<Allocator> allocator_;

  fzl::ResizeableVmoMapper info_mapping_;
  vmoid_t info_vmoid_ = {};

  uint64_t fs_id_ = 0;

  BlobfsMetrics metrics_ = {};

  vmoid_t transfer_vmoid_ = {};
  bool paging_enabled_ = false;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_BLOBFS_H_
