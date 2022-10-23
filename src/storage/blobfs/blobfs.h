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

#include <fidl/fuchsia.blobfs/cpp/wire.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/trace/event.h>
#include <lib/zx/resource.h>
#include <lib/zx/result.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <shared_mutex>

#include <bitmap/raw-bitmap.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <storage/operation/unbuffered_operations_builder.h>

#include "src/lib/digest/digest.h"
#include "src/lib/storage/block_client/cpp/block_device.h"
#include "src/lib/storage/block_client/cpp/client.h"
#include "src/lib/storage/vfs/cpp/journal/journal.h"
#include "src/lib/storage/vfs/cpp/paged_vfs.h"
#include "src/lib/storage/vfs/cpp/vnode.h"
#include "src/storage/blobfs/allocator/allocator.h"
#include "src/storage/blobfs/allocator/extent_reserver.h"
#include "src/storage/blobfs/allocator/node_reserver.h"
#include "src/storage/blobfs/blob_cache.h"
#include "src/storage/blobfs/blob_loader.h"
#include "src/storage/blobfs/blobfs_inspect_tree.h"
#include "src/storage/blobfs/blobfs_metrics.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/compression/external_decompressor.h"
#include "src/storage/blobfs/compression_settings.h"
#include "src/storage/blobfs/directory.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/iterator/allocated_extent_iterator.h"
#include "src/storage/blobfs/iterator/block_iterator.h"
#include "src/storage/blobfs/iterator/block_iterator_provider.h"
#include "src/storage/blobfs/iterator/extent_iterator.h"
#include "src/storage/blobfs/mount.h"
#include "src/storage/blobfs/page_loader.h"
#include "src/storage/blobfs/transaction.h"
#include "src/storage/blobfs/transaction_manager.h"

namespace blobfs {

using block_client::BlockDevice;
using fuchsia_io::wire::FilesystemInfo;

constexpr char kOutgoingDataRoot[] = "root";

class Blobfs : public TransactionManager, public BlockIteratorProvider {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Blobfs);

  // Creates a blobfs object with the default compression algorithm.
  //
  // The dispatcher should be for the current thread that blobfs is running on. The vfs is required
  // for paging but can be null in host configurations. The optional root VM resource is needed to
  // create executable blobs. See vmex_resource() getter.
  static zx::result<std::unique_ptr<Blobfs>> Create(async_dispatcher_t* dispatcher,
                                                    std::unique_ptr<BlockDevice> device,
                                                    fs::PagedVfs* vfs = nullptr,
                                                    const MountOptions& options = MountOptions(),
                                                    zx::resource vmex_resource = zx::resource());

  static std::unique_ptr<BlockDevice> Destroy(std::unique_ptr<Blobfs> blobfs);

  virtual ~Blobfs();

  // The Vfs object associated with this Blobfs instance, if any. The Vfs will exist only when
  // running on the target and will be null otherwise.
  fs::PagedVfs* vfs() const { return vfs_; }

  // TransactionManager's fs::TransactionHandler interface.
  //
  // Allows transmitting read and write transactions directly to the underlying storage.
  uint64_t BlockNumberToDevice(uint64_t block_num) const final {
    return block_num * kBlobfsBlockSize / block_info_.block_size;
  }
  block_client::BlockDevice* GetDevice() final { return block_device_.get(); }

  // TransactionManager's SpaceManager interface.
  //
  // Allows viewing and controlling the size of the underlying volume.
  const Superblock& Info() const final { return info_; }
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) final;
  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final;
  zx_status_t AddInodes(Allocator* allocator) final;
  zx_status_t AddBlocks(size_t nblocks, RawBitmap* block_map) final;

  // Returns filesystem specific information.
  zx::result<fs::FilesystemInfo> GetFilesystemInfo();

  fs_inspect::NodeOperations& node_operations() { return inspect_tree_.node_operations(); }

  // TODO(fxbug.dev/80285): Move ownership of metrics_ to inspect_tree_, and remove use of shared
  // ownership (all uses of this function take mutable pointers to this object already, or bypass
  // the use of shared ownership entirely by calling |get()| on the shared_ptr.
  const std::shared_ptr<BlobfsMetrics>& GetMetrics() const { return metrics_; }

  // TransactionManager interface.
  //
  // Allows attaching VMOs, controlling the underlying volume, and sending transactions to the
  // underlying storage (optionally through the journal).
  fs::Journal* GetJournal() final { return journal_.get(); }

  // BlockIteratorProvider interface.
  //
  // Allows clients to acquire a block iterator for a given node index.
  zx::result<BlockIterator> BlockIteratorByNodeIndex(uint32_t node_index) final;

  static constexpr size_t WriteBufferBlockCount() {
    // Hardcoded to 10 MB; may be replaced by a more device-specific option
    // in the future.
    return 10 * (1 << 20) / kBlobfsBlockSize;
  }

  Writability writability() const { return writability_; }

  // Returns the dispatcher for the current thread that blobfs uses.
  async_dispatcher_t* dispatcher() { return dispatcher_; }

  const CompressionSettings& write_compression_settings() { return write_compression_settings_; }

  bool CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                            uint64_t* first_unset = nullptr) const {
    return allocator_->CheckBlocksAllocated(start_block, end_block, first_unset);
  }

  NodeFinder* GetNodeFinder() { return allocator_.get(); }

  Allocator* GetAllocator() { return allocator_.get(); }

  zx::result<InodePtr> GetNode(uint32_t node_index) { return allocator_->GetNode(node_index); }

  // Invokes "open" on the root directory.
  // Acts as a special-case to bootstrap filesystem mounting.
  zx_status_t OpenRootNode(fbl::RefPtr<fs::Vnode>* out);

  BlobCache& GetCache() { return blob_cache_; }

  zx_status_t Readdir(fs::VdirCookie* cookie, void* dirents, size_t len, size_t* out_actual);

  BlockDevice* Device() const { return block_device_.get(); }

  // Synchronizes the journal and then executes the callback from the journal thread.
  //
  // During shutdown there is no journal but there is nothing to sync. In this case the callback
  // will be issued reentrantly from this function with ZX_OK.
  using SyncCallback = fs::Vnode::SyncCallback;
  void Sync(SyncCallback cb);

  // Frees an inode, from both the reserved map and the inode table. If the
  // inode was allocated in the inode table, write the deleted inode out to
  // disk. Returns an error if the inode could not be freed.
  zx_status_t FreeInode(uint32_t node_index, BlobTransaction& transaction);

  // Writes node data to the inode table and updates disk.
  void PersistNode(uint32_t node_index, BlobTransaction& transaction);

  // Adds reserved blocks to allocated bitmap and writes the bitmap out to disk.
  void PersistBlocks(const ReservedExtent& reserved_extent, BlobTransaction& transaction);

  bool ShouldCompress() const {
    return write_compression_settings_.compression_algorithm != CompressionAlgorithm::kUncompressed;
  }

  // Optional root VM resource. This is necessary to allow executable blobs to be created. It will
  // be a null resource if this blobfs instance does not have access (msotly happens in tests) in
  // which case it will be impossible to create executable memory mappings.
  const zx::resource& vmex_resource() const { return vmex_resource_; }

  BlobLoader& loader() { return *loader_; }
  PageLoader& page_loader() { return *page_loader_; }

  zx_status_t RunRequests(const std::vector<storage::BufferedOperation>& operations) override;

  // Corruption notifier related.
  const BlobCorruptionNotifier& blob_corruption_notifier() { return blob_corruption_notifier_; }
  void SetCorruptBlobHandler(fidl::ClientEnd<fuchsia_blobfs::CorruptBlobHandler> blobfs_handler) {
    blob_corruption_notifier_.set_corruption_handler(std::move(blobfs_handler));
  }

  // Returns an optional overriden cache policy to apply for pager-backed blobs. If unset, the
  // default cache policy should be used.
  std::optional<CachePolicy> pager_backed_cache_policy() const {
    return pager_backed_cache_policy_;
  }

  zx::result<std::unique_ptr<Superblock>> ReadBackupSuperblock();

  // Updates fragmentation metric properties in |out_metrics|. The calculated statistics can also be
  // obtained directly providing |out_stats|.
  //
  // **NOTE**: This function is not thread-safe, and is computationally expensive. It scans through
  // all inodes, extents, and data bitmap entries. Errors are logged but ignored in an attempt to
  // provide as much information as possible (i.e. corrupted extents are skipped).
  void CalculateFragmentationMetrics(FragmentationMetrics& fragmentation_metrics,
                                     FragmentationStats* out_stats = nullptr);

  // Loads the blob with inode |node_index| and verifies that the contents of the blob are valid.
  // Discards the blob's data after performing verification.
  [[nodiscard]] zx_status_t LoadAndVerifyBlob(uint32_t node_index);

  DecompressorCreatorConnector* decompression_connector() { return decompression_connector_; }

  bool use_streaming_writes() const { return use_streaming_writes_; }

  bool allow_offline_compression() const { return allow_offline_compression_; }

 protected:
  // Reloads metadata from disk. Useful when metadata on disk
  // may have changed due to journal playback.
  zx_status_t ReloadSuperblock();

 private:
  friend class BlobfsChecker;
  FidlBlobCorruptionNotifier blob_corruption_notifier_;

  Blobfs(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device, fs::PagedVfs* vfs,
         const Superblock* info, Writability writable,
         CompressionSettings write_compression_settings, zx::resource vmex_resource,
         std::optional<CachePolicy> pager_backed_cache_policy,
         DecompressorCreatorConnector* decompression_connector, bool use_streaming_writes,
         bool allow_offline_compression);

  static zx::result<std::unique_ptr<fs::Journal>> InitializeJournal(
      fs::TransactionHandler* transaction_handler, VmoidRegistry* registry, uint64_t journal_start,
      uint64_t journal_length, fs::JournalSuperblock journal_superblock);

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
  void FreeExtent(const Extent& extent, BlobTransaction& transaction);

  // Free a single node. Doesn't attempt to parse the type / traverse nodes;
  // this function just deletes a single node.
  zx_status_t FreeNode(uint32_t node_index, BlobTransaction& transaction);

  // Given a contiguous number of blocks after a starting block,
  // write out the bitmap to disk for the corresponding blocks.
  // Should only be called by PersistBlocks and FreeExtent.
  void WriteBitmap(uint64_t nblocks, uint64_t start_block, BlobTransaction& transaction);

  // Given a node within the node map at an index, write it to disk.
  // Should only be called by AllocateNode and FreeNode.
  void WriteNode(uint32_t map_index, BlobTransaction& transaction);

  // Enqueues into |transaction| a write to set the on-disk superblock to the current state in
  // |info_|. If |write_backup| is true, then also write the backup superblock (which should
  // only be necessary if the backup superblock becomes corrupted for some reason).
  void WriteInfo(BlobTransaction& transaction, bool write_backup = false);

  // Adds a trim operation to |transaction|.
  void DeleteExtent(uint64_t start_block, uint64_t num_blocks, BlobTransaction& transaction) const;

  // Runs fsck at the end of a transaction, just after metadata has been written. Used for testing
  // to be sure that all transactions leave the file system in a good state.
  void FsckAtEndOfTransaction();

  // Sequentually migrates blobfs to the latest oldest_minor_version.
  // Performs zero or more passes which each migrate blobfs from oldest_minor_version N -> N+1 by
  // performing some reparative action. See MigrateToRevN.
  zx_status_t Migrate();

  // Repairs the Inode of the null blob (if present) to not have a zero-length extent.
  // NOP if oldest_minor_version >= kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly.
  zx_status_t MigrateToRev4();

  // Walks all the extents of a given inode, updating |extents_per_blob| and |used_fragments|
  // metrics in |out_metrics|. If |out_stats| is provided, also records those values there.
  void ComputeBlobFragmentation(uint32_t node_index, Inode& inode,
                                FragmentationMetrics& fragmentation_metrics,
                                FragmentationStats* out_stats);

  // Possibly-null reference to the Vfs associated with this object. See vfs() getter.
  fs::PagedVfs* vfs_ = nullptr;

  // Journal object is only created if the filesystem is mounted as writable.
  std::unique_ptr<fs::Journal> journal_;
  Superblock info_;

  BlobCache blob_cache_;

  // Dispatcher for the thread this object is running on.
  async_dispatcher_t* dispatcher_ = nullptr;

  std::unique_ptr<BlockDevice> block_device_;
  fuchsia_hardware_block_BlockInfo block_info_ = {};
  Writability writability_;
  const CompressionSettings write_compression_settings_;
  zx::resource vmex_resource_;  // Possibly null resource. See getter for more.

  std::unique_ptr<Allocator> allocator_;

  fzl::ResizeableVmoMapper info_mapping_;
  storage::Vmoid info_vmoid_;

  // This event's koid is used as a unique identifier for this filesystem instance.
  zx::event fs_id_;

  BlobfsInspectTree inspect_tree_;

  std::shared_ptr<BlobfsMetrics> metrics_;  // Guaranteed non-null.

  // Initialize all inspect properties in `inspect_tree_`. Should only be called after the
  // filesystem has been initialized successfully.
  void InitializeInspectTree();

  std::unique_ptr<PageLoader> page_loader_;  // Guaranteed non-null after Create() succeeds.
  std::optional<CachePolicy> pager_backed_cache_policy_;

  std::unique_ptr<BlobLoader> loader_;
  std::shared_mutex fsck_at_end_of_transaction_mutex_;

  DecompressorCreatorConnector* decompression_connector_;

  const bool use_streaming_writes_;
  const bool allow_offline_compression_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOBFS_H_
