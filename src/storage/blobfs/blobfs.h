// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the global Blobfs structure used for constructing a Blobfs filesystem in
// memory.

#ifndef SRC_STORAGE_BLOBFS_BLOBFS_H_
#define SRC_STORAGE_BLOBFS_BLOBFS_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fuchsia/blobfs/llcpp/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/trace/event.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <shared_mutex>

#include <bitmap/raw-bitmap.h>
#include <blobfs/common.h>
#include <blobfs/compression-settings.h>
#include <blobfs/format.h>
#include <blobfs/mount.h>
#include <block-client/cpp/block-device.h>
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
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <storage/operation/unbuffered_operations_builder.h>

#include "allocator/allocator.h"
#include "allocator/extent-reserver.h"
#include "allocator/node-reserver.h"
#include "blob-cache.h"
#include "blob-loader.h"
#include "compression/zstd-seekable-blob-collection.h"
#include "directory.h"
#include "iterator/allocated-extent-iterator.h"
#include "iterator/block-iterator-provider.h"
#include "iterator/block-iterator.h"
#include "iterator/extent-iterator.h"
#include "metrics.h"
#include "pager/user-pager.h"
#include "transaction-manager.h"

namespace blobfs {

using block_client::BlockDevice;
using llcpp::fuchsia::io::FilesystemInfo;
using storage::UnbufferedOperationsBuilder;

constexpr char kOutgoingDataRoot[] = "root";

class Blobfs : public TransactionManager, public BlockIteratorProvider {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Blobfs);

  // Creates a blobfs object with the default compression algorithm.
  //
  // The dispatcher should be for the current thread that blobfs is running on.
  static zx_status_t Create(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device,
                            MountOptions* options, zx::resource vmex_resource,
                            std::unique_ptr<Blobfs>* out);

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
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) final;
  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final;
  zx_status_t AddInodes(Allocator* allocator) final;
  zx_status_t AddBlocks(size_t nblocks, RawBitmap* block_map) final;

  // Returns filesystem specific information.
  void GetFilesystemInfo(FilesystemInfo* info) const;

  ////////////////
  // TransactionManager interface.
  //
  // Allows attaching VMOs, controlling the underlying volume, and sending transactions to the
  // underlying storage (optionally through the journal).

  BlobfsMetrics* Metrics() final { return &metrics_; }
  size_t WritebackCapacity() const final;
  fs::Journal* journal() final;
  Writability writability() const { return writability_; }
  ZSTDSeekableBlobCollection* zstd_seekable_blob_collection() {
    return zstd_seekable_blob_collection_.get();
  }

  ////////////////
  // BlockIteratorProvider interface.
  //
  // Allows clients to acquire a block iterator for a given node index.

  BlockIterator BlockIteratorByNodeIndex(uint32_t node_index) final;

  ////////////////
  // Other methods.

  // Returns the dispatcher for the current thread that blobfs uses.
  async_dispatcher_t* dispatcher() { return dispatcher_; }

  const CompressionSettings& write_compression_settings() { return write_compression_settings_; }

  bool CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                            uint64_t* first_unset = nullptr) const {
    return allocator_->CheckBlocksAllocated(start_block, end_block, first_unset);
  }

  NodeFinder* GetNodeFinder() { return allocator_.get(); }

  Allocator* GetAllocator() { return allocator_.get(); }

  InodePtr GetNode(uint32_t node_index) { return allocator_->GetNode(node_index); }

  // Invokes "open" on the root directory.
  // Acts as a special-case to bootstrap filesystem mounting.
  zx_status_t OpenRootNode(fbl::RefPtr<fs::Vnode>* out);

  BlobCache& Cache() { return blob_cache_; }

  zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual);

  BlockDevice* Device() const { return block_device_.get(); }

  // Returns an unique identifier for this instance. Each invocation returns a new
  // handle that references the same kernel object, which is used as the identifier.
  zx_status_t GetFsId(zx::event* out_fs_id) const;
  uint64_t GetFsIdLegacy() const { return fs_id_legacy_; }

  // Synchronizes the journal and then executes the callback from the journal thread.
  //
  // During shutdown there is no journal but there is nothing to sync. In this case the callback
  // will be issued reentrantly from this function with ZX_OK.
  using SyncCallback = fs::Vnode::SyncCallback;
  void Sync(SyncCallback cb);

  // Frees an inode, from both the reserved map and the inode table. If the
  // inode was allocated in the inode table, write the deleted inode out to
  // disk.
  void FreeInode(uint32_t node_index, storage::UnbufferedOperationsBuilder* operations,
                 std::vector<storage::BufferedOperation>* trim_data);

  // Writes node data to the inode table and updates disk.
  void PersistNode(uint32_t node_index, storage::UnbufferedOperationsBuilder* operations);

  // Adds reserved blocks to allocated bitmap and writes the bitmap out to disk.
  void PersistBlocks(const ReservedExtent& reserved_extent,
                     storage::UnbufferedOperationsBuilder* operations);

  bool PagingEnabled() const { return pager_ != nullptr; }

  bool ShouldCompress() const {
    return write_compression_settings_.compression_algorithm != CompressionAlgorithm::UNCOMPRESSED;
  }

  const zx::resource& vmex_resource() const { return vmex_resource_; }

  BlobLoader& loader() { return loader_; }

  zx_status_t RunRequests(const std::vector<storage::BufferedOperation>& operations) override;

  // Corruption notifier related.
  const BlobCorruptionNotifier* GetCorruptBlobNotifier(void) {
    return blob_corruption_notifier_.get();
  }
  void SetCorruptBlobHandler(zx::channel blobfs_handler) {
    blob_corruption_notifier_->SetCorruptBlobHandler(std::move(blobfs_handler));
  }

  // Returns an optional overriden cache policy to apply for pager-backed blobs. If unset, the
  // default cache policy should be used.
  std::optional<CachePolicy> pager_backed_cache_policy() const {
    return pager_backed_cache_policy_;
  };

 protected:
  // Reloads metadata from disk. Useful when metadata on disk
  // may have changed due to journal playback.
  zx_status_t ReloadSuperblock();

 private:
  friend class BlobfsChecker;
  std::unique_ptr<BlobCorruptionNotifier> blob_corruption_notifier_;

  Blobfs(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device,
         const Superblock* info, Writability writable,
         CompressionSettings write_compression_settings, zx::resource vmex_resource,
         std::optional<CachePolicy> pager_backed_cache_policy);

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
  [[nodiscard]] zx_status_t InitializeVnodes();

  // Frees blocks from the allocated map (if allocated) and updates disk if necessary.
  void FreeExtent(const Extent& extent, storage::UnbufferedOperationsBuilder* operations,
                  std::vector<storage::BufferedOperation>* trim_data);

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
                    std::vector<storage::BufferedOperation>* trim_data) const;

  // Creates an unique identifier for this instance. This is to be called only during
  // "construction".
  [[nodiscard]] zx_status_t CreateFsId();

  // Loads the blob with inode |node_index| and verifies that the contents of the blob are valid.
  // Discards the blob's data after performing verification.
  [[nodiscard]] zx_status_t LoadAndVerifyBlob(uint32_t node_index);

  // Updates the flags field in superblock.
  void UpdateFlags(storage::UnbufferedOperationsBuilder* operations, uint32_t flags, bool set);

  // Runs fsck at the end of a transaction, just after metadata has been written. Used for testing
  // to be sure that all transactions leave the file system in a good state.
  void FsckAtEndOfTransaction(zx_status_t status);

  static BlobfsMetrics CreateMetrics();

  std::unique_ptr<fs::Journal> journal_;
  Superblock info_;

  BlobCache blob_cache_;

  // Dispatcher for the thread this object is running on.
  async_dispatcher_t* dispatcher_ = nullptr;

  std::unique_ptr<BlockDevice> block_device_;
  fuchsia_hardware_block_BlockInfo block_info_ = {};
  Writability writability_;
  const CompressionSettings write_compression_settings_;
  zx::resource vmex_resource_;

  std::unique_ptr<Allocator> allocator_;

  fzl::ResizeableVmoMapper info_mapping_;
  storage::Vmoid info_vmoid_;

  // A unique identifier for this filesystem instance.
  zx::event fs_id_;

  std::unique_ptr<ZSTDSeekableBlobCollection> zstd_seekable_blob_collection_ = nullptr;

  // The numerical version of fs_id is used by the old |fuchsia.io/DirectoryAdmin| protocol,
  // which is being deprecated in favor of |fuchsia.fs/Query|. It is derived from |fs_id|
  // by inspecting its koid.
  uint64_t fs_id_legacy_ = 0;

  BlobfsMetrics metrics_ = CreateMetrics();

  std::unique_ptr<pager::UserPager> pager_ = nullptr;
  std::optional<CachePolicy> pager_backed_cache_policy_ = std::nullopt;

  BlobLoader loader_;
  std::shared_mutex fsck_at_end_of_transaction_mutex_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOBFS_H_
