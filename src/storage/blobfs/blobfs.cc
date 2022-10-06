// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blobfs.h"

#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <inttypes.h>
#include <lib/async/cpp/task.h>
#include <lib/cksum.h>
#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/event.h>
#include <lib/zx/status.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <limits>
#include <memory>
#include <string_view>
#include <utility>

#include <fbl/ref_ptr.h>
#include <safemath/safe_conversions.h>

#include "src/lib/digest/digest.h"
#include "src/lib/digest/merkle-tree.h"
#include "src/lib/storage/block_client/cpp/pass_through_read_only_device.h"
#include "src/lib/storage/block_client/cpp/reader.h"
#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/lib/storage/vfs/cpp/journal/journal.h"
#include "src/lib/storage/vfs/cpp/journal/replay.h"
#include "src/lib/storage/vfs/cpp/journal/superblock.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/scoped_vnode_open.h"
#include "src/lib/storage/vfs/cpp/ticker.h"
#include "src/storage/blobfs/allocator/extent_reserver.h"
#include "src/storage/blobfs/allocator/node_reserver.h"
#include "src/storage/blobfs/blob.h"
#include "src/storage/blobfs/blob_loader.h"
#include "src/storage/blobfs/blobfs_checker.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/compression/compressor.h"
#include "src/storage/blobfs/compression_settings.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/fsck.h"
#include "src/storage/blobfs/iterator/allocated_extent_iterator.h"
#include "src/storage/blobfs/iterator/allocated_node_iterator.h"
#include "src/storage/blobfs/iterator/block_iterator.h"
#include "src/storage/blobfs/loader_info.h"
#include "src/storage/blobfs/transaction.h"
#include "src/storage/blobfs/transfer_buffer.h"
#include "src/storage/fvm/client.h"

namespace blobfs {
namespace {

using ::digest::Digest;
using ::fs::Journal;
using ::fs::JournalSuperblock;
using ::id_allocator::IdAllocator;
using ::storage::BlockingRingBuffer;
using ::storage::VmoidRegistry;

struct DirectoryCookie {
  size_t index;       // Index into node map
  uint64_t reserved;  // Unused
};

const char* CachePolicyToString(CachePolicy policy) {
  switch (policy) {
    case CachePolicy::NeverEvict:
      return "NEVER_EVICT";
    case CachePolicy::EvictImmediately:
      return "EVICT_IMMEDIATELY";
  }
}

zx_status_t LoadSuperblock(const fuchsia_hardware_block_BlockInfo& block_info, int block_offset,
                           BlockDevice& device, char block[kBlobfsBlockSize]) {
  block_client::Reader reader(device);
  zx_status_t status = reader.Read(block_offset * kBlobfsBlockSize, kBlobfsBlockSize, block);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "could not read info block: " << zx_status_get_string(status);
    return status;
  }
  const Superblock* superblock = reinterpret_cast<Superblock*>(block);

  uint64_t blocks = (block_info.block_size * block_info.block_count) / kBlobfsBlockSize;
  if (kBlobfsBlockSize % block_info.block_size != 0) {
    FX_LOGS(ERROR) << "Blobfs block size (" << kBlobfsBlockSize
                   << ") not divisible by device block size (" << block_info.block_size << ")";
    return ZX_ERR_IO;
  }

  return CheckSuperblock(superblock, blocks, /*quiet=*/false);
}

std::shared_ptr<BlobfsMetrics> CreateBlobfsMetrics(inspect::Inspector inspector) {
  bool enable_page_in_metrics = false;
#ifdef BLOBFS_ENABLE_PAGE_IN_METRICS
  enable_page_in_metrics = true;
#endif
  return std::make_shared<BlobfsMetrics>(enable_page_in_metrics, std::move(inspector));
}

}  // namespace

zx::status<std::unique_ptr<Blobfs>> Blobfs::Create(async_dispatcher_t* dispatcher,
                                                   std::unique_ptr<BlockDevice> device,
                                                   fs::PagedVfs* vfs, const MountOptions& options,
                                                   zx::resource vmex_resource) {
  TRACE_DURATION("blobfs", "Blobfs::Create");

  fuchsia_hardware_block_BlockInfo block_info;
  if (zx_status_t status = device->BlockGetInfo(&block_info); status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot acquire block info: " << status;
    return zx::error(status);
  }

  if (block_info.flags & BLOCK_FLAG_READONLY &&
      (options.writability != blobfs::Writability::ReadOnlyDisk)) {
    return zx::error(ZX_ERR_ACCESS_DENIED);
  }

  bool fvm_required = false;
  char block[kBlobfsBlockSize];

  if (zx_status_t status1 = LoadSuperblock(block_info, kSuperblockOffset, *device, block);
      status1 != ZX_OK) {
    FX_LOGS(WARNING) << "Trying backup superblock";
    if (zx_status_t status2 =
            LoadSuperblock(block_info, kFVMBackupSuperblockOffset, *device, block);
        status2 != ZX_OK) {
      FX_LOGS(ERROR) << "No good superblock found";
      return zx::error(status1);  // Return the first error we found.
    }
    // Backup superblocks are only valid with FVM.
    fvm_required = true;
  }
  const Superblock* superblock = reinterpret_cast<Superblock*>(block);

  DecompressorCreatorConnector* decompression_connector = nullptr;
  if (options.sandbox_decompression) {
    decompression_connector = options.decompression_connector
                                  ? options.decompression_connector
                                  : &DecompressorCreatorConnector::DefaultServiceConnector();
  }

  // Construct the Blobfs object, without intensive validation, since it
  // may require upgrades / journal replays to become valid.
  auto fs = std::unique_ptr<Blobfs>(new Blobfs(
      dispatcher, std::move(device), vfs, superblock, options.writability,
      options.compression_settings, std::move(vmex_resource), options.pager_backed_cache_policy,
      decompression_connector, options.streaming_writes, options.offline_compression));
  fs->block_info_ = block_info;

  auto fs_ptr = fs.get();
  FX_CHECK(options.paging_threads > 0);
  std::vector<std::unique_ptr<PageLoader::WorkerResources>> worker_resources;
  for (int i = 0; i < options.paging_threads; ++i) {
    auto uncompressed_buffer_or = StorageBackedTransferBuffer::Create(
        kTransferBufferSize, fs_ptr, fs_ptr, fs_ptr->GetMetrics().get());
    if (!uncompressed_buffer_or.is_ok()) {
      FX_LOGS(ERROR) << "Could not initialize uncompressed pager transfer buffer";
      return uncompressed_buffer_or.take_error();
    }
    auto compressed_buffer_or = StorageBackedTransferBuffer::Create(
        kTransferBufferSize, fs_ptr, fs_ptr, fs_ptr->GetMetrics().get());
    if (compressed_buffer_or.is_error()) {
      FX_LOGS(ERROR) << "Could not initialize compressed pager transfer buffer";
      return compressed_buffer_or.take_error();
    }
    worker_resources.push_back(std::make_unique<PageLoader::WorkerResources>(
        std::move(uncompressed_buffer_or).value(), std::move(compressed_buffer_or).value()));
  }
  auto page_loader_or =
      PageLoader::Create(std::move(worker_resources), kDecompressionBufferSize,
                         fs_ptr->GetMetrics().get(), fs->decompression_connector());
  if (page_loader_or.is_error()) {
    FX_LOGS(ERROR) << "Could not initialize user pager";
    return page_loader_or.take_error();
  }
  fs->page_loader_ = std::move(page_loader_or).value();
  FX_LOGS(INFO) << "Initialized user pager with " << options.paging_threads << " threads";

  JournalSuperblock journal_superblock;
  if (options.writability != blobfs::Writability::ReadOnlyDisk) {
    FX_LOGS(INFO) << "Replaying journal";
    auto journal_superblock_or = fs::ReplayJournal(fs.get(), fs.get(), JournalStartBlock(fs->info_),
                                                   JournalBlocks(fs->info_), kBlobfsBlockSize);
    if (journal_superblock_or.is_error()) {
      FX_LOGS(ERROR) << "Failed to replay journal";
      return journal_superblock_or.take_error();
    }
    journal_superblock = std::move(journal_superblock_or.value());
    FX_LOGS(DEBUG) << "Journal replayed";
    if (zx_status_t status = fs->ReloadSuperblock(); status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to re-load superblock";
      return zx::error(status);
    }
    if ((fs->Info().major_version >= kBlobfsCompactMerkleTreeVersion ||
         fs->Info().oldest_minor_version >= kBlobfsMinorVersionNoOldCompressionFormats) &&
        options.compression_settings.compression_algorithm != CompressionAlgorithm::kChunked &&
        options.compression_settings.compression_algorithm != CompressionAlgorithm::kUncompressed) {
      FX_LOGS(ERROR) << "Unsupported compression algorithm";
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }

  if (fvm_required && (fs->Info().flags & kBlobFlagFVM) == 0) {
    FX_LOGS(ERROR) << "FVM required but superblock indicates otherwise";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  switch (options.writability) {
    case blobfs::Writability::Writable: {
      FX_LOGS(DEBUG) << "Initializing journal for writeback";
      auto journal_or = InitializeJournal(fs.get(), fs.get(), JournalStartBlock(fs->info_),
                                          JournalBlocks(fs->info_), std::move(journal_superblock));
      if (journal_or.is_error()) {
        FX_LOGS(ERROR) << "Failed to initialize journal";
        return journal_or.take_error();
      }
      fs->journal_ = std::move(journal_or.value());
#ifndef NDEBUG
      if (options.fsck_at_end_of_every_transaction) {
        fs->journal_->set_write_metadata_callback(
            fit::bind_member<&Blobfs::FsckAtEndOfTransaction>(fs.get()));
      }
#endif
      break;
    }
    case blobfs::Writability::ReadOnlyDisk:
    case blobfs::Writability::ReadOnlyFilesystem:
      // Journal uninitialized.
      break;
  }

  // Validate the FVM after replaying the journal.
  zx_status_t status =
      CheckFvmConsistency(&fs->info_, fs->Device(),
                          /*repair=*/options.writability != blobfs::Writability::ReadOnlyDisk);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "FVM info check failed";
    return zx::error(status);
  }

  FX_LOGS(INFO) << "Using eviction policy " << CachePolicyToString(options.cache_policy);
  if (options.pager_backed_cache_policy) {
    FX_LOGS(INFO) << "Using overridden pager eviction policy "
                  << CachePolicyToString(*options.pager_backed_cache_policy);
  }
  fs->GetCache().SetCachePolicy(options.cache_policy);

  RawBitmap block_map;
  // Keep the block_map aligned to a block multiple
  if ((status = block_map.Reset(BlockMapBlocks(fs->info_) * kBlobfsBlockBits)) < 0) {
    FX_LOGS(ERROR) << "Could not reset block bitmap";
    return zx::error(status);
  }
  if ((status = block_map.Shrink(fs->info_.data_block_count)) < 0) {
    FX_LOGS(ERROR) << "Could not shrink block bitmap";
    return zx::error(status);
  }
  fzl::ResizeableVmoMapper node_map;

  size_t nodemap_size = kBlobfsInodeSize * fs->info_.inode_count;
  ZX_DEBUG_ASSERT(fbl::round_up(nodemap_size, kBlobfsBlockSize) == nodemap_size);
  ZX_DEBUG_ASSERT(nodemap_size / kBlobfsBlockSize == NodeMapBlocks(fs->info_));
  if ((status = node_map.CreateAndMap(nodemap_size, "nodemap")) != ZX_OK) {
    return zx::error(status);
  }
  std::unique_ptr<IdAllocator> nodes_bitmap = {};
  if ((status = IdAllocator::Create(fs->info_.inode_count, &nodes_bitmap)) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to allocate bitmap for inodes: " << zx_status_get_string(status);
    return zx::error(status);
  }

  fs->allocator_ = std::make_unique<Allocator>(fs.get(), std::move(block_map), std::move(node_map),
                                               std::move(nodes_bitmap));
  if ((status = fs->allocator_->ResetFromStorage(*fs)) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to load bitmaps: " << zx_status_get_string(status);
    return zx::error(status);
  }
  if ((status = fs->info_mapping_.CreateAndMap(kBlobfsBlockSize, "blobfs-superblock")) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create info vmo: " << zx_status_get_string(status);
    return zx::error(status);
  }
  if ((status = fs->BlockAttachVmo(fs->info_mapping_.vmo(), &fs->info_vmoid_)) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to attach info vmo: " << zx_status_get_string(status);
    return zx::error(status);
  }
  if ((status = fs->InitializeVnodes()) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize Vnodes: " << zx_status_get_string(status);
    return zx::error(status);
  }
  zx::status<std::unique_ptr<BlobLoader>> loader_or = BlobLoader::Create(
      fs_ptr, fs_ptr, fs->GetNodeFinder(), fs->GetMetrics(), fs->decompression_connector());
  if (loader_or.is_error()) {
    FX_LOGS(ERROR) << "Failed to initialize loader: " << loader_or.status_string();
    return loader_or.take_error();
  }
  fs->loader_ = *std::move(loader_or);

  // At this point, the filesystem is loaded and validated. No errors should be returned after this
  // point.

  // On a read-write filesystem, since we can now serve writes, we need to unset the kBlobFlagClean
  // flag to indicate that the filesystem may not be in a "clean" state anymore. This helps to make
  // sure we are unmounted cleanly i.e the kBlobFlagClean flag is set back on clean unmount.
  //
  // Additionally, we can now update the oldest_minor_version field if it needs to be updated.
  FX_LOGS(INFO) << "detected oldest_minor_version " << fs->info_.oldest_minor_version
                << ", current minor version " << kBlobfsCurrentMinorVersion;
  if (options.writability == blobfs::Writability::Writable) {
    BlobTransaction transaction;
    fs->info_.flags &= ~kBlobFlagClean;
    if (fs->info_.oldest_minor_version > kBlobfsCurrentMinorVersion) {
      FX_LOGS(INFO) << "Setting oldest_minor_version to " << kBlobfsCurrentMinorVersion;
      fs->info_.oldest_minor_version = kBlobfsCurrentMinorVersion;
    }
    // Write a backup superblock if there's an old version of blobfs.
    bool write_backup = false;
    if (fs->info_.oldest_minor_version < kBlobfsMinorVersionBackupSuperblock) {
      FX_LOGS(INFO) << "Upgrading to revision " << kBlobfsMinorVersionBackupSuperblock;
      if (fs->Info().flags & kBlobFlagFVM) {
        FX_LOGS(INFO) << "Writing backup superblock";
        write_backup = true;
      }
      fs->info_.oldest_minor_version = kBlobfsMinorVersionBackupSuperblock;
    }
    fs->WriteInfo(transaction, write_backup);
    transaction.Commit(*fs->GetJournal());
  }

  FX_LOGS(INFO) << "Using compression "
                << CompressionAlgorithmToString(
                       fs->write_compression_settings_.compression_algorithm);
  if (fs->write_compression_settings_.compression_level) {
    FX_LOGS(INFO) << "Using overridden compression level "
                  << *(fs->write_compression_settings_.compression_level);
  }

  FX_LOGS(INFO) << "Using blob layout format: "
                << BlobLayoutFormatToString(GetBlobLayoutFormat(*superblock));

  if (zx_status_t status = fs->Migrate(); status != ZX_OK) {
    return zx::error(status);
  }

  fs->InitializeInspectTree();

  return zx::ok(std::move(fs));
}

void Blobfs::InitializeInspectTree() {
  fs_inspect::InfoData info{
      .version_major = kBlobfsCurrentMajorVersion,
      .version_minor = kBlobfsCurrentMinorVersion,
      .oldest_version =
          fs_inspect::InfoData::OldestVersion(Info().major_version, Info().oldest_minor_version),
  };

  zx::status<fs::FilesystemInfo> fs_info{GetFilesystemInfo()};
  if (fs_info.is_error()) {
    FX_LOGS(ERROR) << "Failed to get filesystem info while initializing inspect tree: "
                   << fs_info.status_string();
  } else {
    info.id = fs_info->fs_id;
    info.type = static_cast<uint32_t>(fs_info->fs_type);
    info.name = fs_info->name;
    info.block_size = fs_info->block_size;
    info.max_filename_length = fs_info->max_filename_size;
  }

  inspect_tree_.SetInfo(info);
  inspect_tree_.UpdateSuperblock(Info());
  block_client::BlockDevice* device = Device();
  if (device) {
    inspect_tree_.UpdateVolumeData(*device);
  }

  inspect_tree_.CalculateFragmentationMetrics(*this);
}

// Writeback enabled, journaling enabled.
zx::status<std::unique_ptr<Journal>> Blobfs::InitializeJournal(
    fs::TransactionHandler* transaction_handler, VmoidRegistry* registry, uint64_t journal_start,
    uint64_t journal_length, JournalSuperblock journal_superblock) {
  const uint64_t journal_entry_blocks = journal_length - fs::kJournalMetadataBlocks;

  std::unique_ptr<BlockingRingBuffer> journal_buffer;
  zx_status_t status = BlockingRingBuffer::Create(registry, journal_entry_blocks, kBlobfsBlockSize,
                                                  "journal-writeback-buffer", &journal_buffer);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot create journal buffer: " << zx_status_get_string(status);
    return zx::error(status);
  }

  std::unique_ptr<BlockingRingBuffer> writeback_buffer;
  status = BlockingRingBuffer::Create(registry, WriteBufferBlockCount(), kBlobfsBlockSize,
                                      "data-writeback-buffer", &writeback_buffer);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot create writeback buffer: " << zx_status_get_string(status);
    return zx::error(status);
  }

  return zx::ok(std::make_unique<Journal>(transaction_handler, std::move(journal_superblock),
                                          std::move(journal_buffer), std::move(writeback_buffer),
                                          journal_start));
}

std::unique_ptr<BlockDevice> Blobfs::Destroy(std::unique_ptr<Blobfs> blobfs) {
  return blobfs->Reset();
}

Blobfs::~Blobfs() { Reset(); }

zx_status_t Blobfs::LoadAndVerifyBlob(uint32_t node_index) {
  auto inode = GetNode(node_index);
  if (inode.is_error()) {
    return inode.status_value();
  }

  // Create a blob and open it (required for verification).
  fbl::RefPtr<Blob> blob = fbl::MakeRefCounted<Blob>(this, node_index, *inode.value());
  fs::ScopedVnodeOpen opener;
  if (zx_status_t status = opener.Open(blob); status != ZX_OK)
    return status;
  return blob->Verify();
}

void Blobfs::PersistBlocks(const ReservedExtent& reserved_extent, BlobTransaction& transaction) {
  TRACE_DURATION("blobfs", "Blobfs::PersistBlocks");

  allocator_->MarkBlocksAllocated(reserved_extent);

  const Extent& extent = reserved_extent.extent();
  info_.alloc_block_count += extent.Length();
  // Write out to disk.
  WriteBitmap(extent.Length(), extent.Start(), transaction);
  WriteInfo(transaction);
}

// Frees blocks from reserved and allocated maps, updates disk in the latter case.
void Blobfs::FreeExtent(const Extent& extent, BlobTransaction& transaction) {
  size_t start = extent.Start();
  size_t num_blocks = extent.Length();
  size_t end = start + num_blocks;

  TRACE_DURATION("blobfs", "Blobfs::FreeExtent", "nblocks", num_blocks, "blkno", start);

  // Check if blocks were allocated on disk.
  if (allocator_->CheckBlocksAllocated(start, end)) {
    transaction.AddReservedExtent(allocator_->FreeBlocks(extent));
    info_.alloc_block_count -= num_blocks;
    WriteBitmap(num_blocks, start, transaction);
    WriteInfo(transaction);
    DeleteExtent(DataStartBlock(info_) + start, num_blocks, transaction);
  }
}

zx_status_t Blobfs::FreeNode(uint32_t node_index, BlobTransaction& transaction) {
  if (zx_status_t status = allocator_->FreeNode(node_index); status != ZX_OK) {
    return status;
  }
  info_.alloc_inode_count--;
  WriteNode(node_index, transaction);
  return ZX_OK;
}

zx_status_t Blobfs::FreeInode(uint32_t node_index, BlobTransaction& transaction) {
  TRACE_DURATION("blobfs", "Blobfs::FreeInode", "node_index", node_index);
  auto mapped_inode = GetNode(node_index);
  if (mapped_inode.is_error()) {
    return mapped_inode.status_value();
  }

  if (mapped_inode->header.IsAllocated()) {
    auto extent_iter = AllocatedExtentIterator::Create(allocator_.get(), node_index);
    if (extent_iter.is_error()) {
      return extent_iter.status_value();
    }

    if (zx_status_t status = FreeNode(node_index, transaction); status != ZX_OK) {
      return status;
    }

    while (!extent_iter->Done()) {
      // If we're observing a new node, free it.
      if (extent_iter->NodeIndex() != node_index) {
        node_index = extent_iter->NodeIndex();
        if (zx_status_t status = FreeNode(node_index, transaction); status != ZX_OK) {
          return status;
        }
      }

      auto extent_or = extent_iter->Next();
      if (extent_or.is_error()) {
        return extent_or.status_value();
      }
      const Extent* extent = extent_or.value();

      // Free the extent.
      FreeExtent(*extent, transaction);
    }
    WriteInfo(transaction);
  }
  return ZX_OK;
}

void Blobfs::PersistNode(uint32_t node_index, BlobTransaction& transaction) {
  TRACE_DURATION("blobfs", "Blobfs::PersistNode");
  info_.alloc_inode_count++;
  // Update inspect data to reflect new used inode count.
  inspect_tree_.UpdateSuperblock(Info());
  WriteNode(node_index, transaction);
  WriteInfo(transaction);
}

void Blobfs::WriteBitmap(uint64_t nblocks, uint64_t start_block, BlobTransaction& transaction) {
  TRACE_DURATION("blobfs", "Blobfs::WriteBitmap", "nblocks", nblocks, "start_block", start_block);
  uint64_t bbm_start_block = start_block / kBlobfsBlockBits;
  uint64_t bbm_end_block =
      fbl::round_up(start_block + nblocks, kBlobfsBlockBits) / kBlobfsBlockBits;

  // Write back the block allocation bitmap
  transaction.AddOperation({.vmo = zx::unowned_vmo(allocator_->GetBlockMapVmo().get()),
                            .op = {
                                .type = storage::OperationType::kWrite,
                                .vmo_offset = bbm_start_block,
                                .dev_offset = BlockMapStartBlock(info_) + bbm_start_block,
                                .length = bbm_end_block - bbm_start_block,
                            }});
}

void Blobfs::WriteNode(uint32_t map_index, BlobTransaction& transaction) {
  TRACE_DURATION("blobfs", "Blobfs::WriteNode", "map_index", map_index);
  uint64_t block = (map_index * sizeof(Inode)) / kBlobfsBlockSize;
  transaction.AddOperation({.vmo = zx::unowned_vmo(allocator_->GetNodeMapVmo().get()),
                            .op = {
                                .type = storage::OperationType::kWrite,
                                .vmo_offset = block,
                                .dev_offset = NodeMapStartBlock(info_) + block,
                                .length = 1,
                            }});
}

void Blobfs::WriteInfo(BlobTransaction& transaction, bool write_backup) {
  memcpy(info_mapping_.start(), &info_, sizeof(info_));
  storage::UnbufferedOperation operation = {
      .vmo = zx::unowned_vmo(info_mapping_.vmo().get()),
      .op =
          {
              .type = storage::OperationType::kWrite,
              .vmo_offset = 0,
              .dev_offset = 0,
              .length = 1,
          },
  };
  transaction.AddOperation(operation);
  if (write_backup) {
    ZX_ASSERT(info_.flags & kBlobFlagFVM);
    operation.op.dev_offset = kFVMBackupSuperblockOffset;
    transaction.AddOperation(operation);
  }
  // Update inspect data to reflect new data block/node counts.
  inspect_tree_.UpdateSuperblock(Info());
}

void Blobfs::DeleteExtent(uint64_t start_block, uint64_t num_blocks,
                          BlobTransaction& transaction) const {
  if (block_info_.flags & fuchsia_hardware_block_FLAG_TRIM_SUPPORT) {
    TRACE_DURATION("blobfs", "Blobfs::DeleteExtent", "num_blocks", num_blocks, "start_block",
                   start_block);
    storage::BufferedOperation operation = {};
    operation.op.type = storage::OperationType::kTrim;
    operation.op.dev_offset = start_block;
    operation.op.length = num_blocks;
    transaction.AddTrimOperation(operation);
  }
}

static_assert(sizeof(DirectoryCookie) <= sizeof(fs::VdirCookie),
              "Blobfs dircookie too large to fit in IO state");

zx_status_t Blobfs::Readdir(fs::VdirCookie* cookie, void* dirents, size_t len, size_t* out_actual) {
  TRACE_DURATION("blobfs", "Blobfs::Readdir", "len", len);
  fs::DirentFiller df(dirents, len);
  DirectoryCookie* c = reinterpret_cast<DirectoryCookie*>(cookie);

  for (size_t i = c->index; i < info_.inode_count; ++i) {
    ZX_DEBUG_ASSERT(i < std::numeric_limits<uint32_t>::max());
    uint32_t node_index = safemath::checked_cast<uint32_t>(i);
    if (GetNode(node_index)->header.IsAllocated() &&
        !GetNode(node_index)->header.IsExtentContainer()) {
      Digest digest(GetNode(node_index)->merkle_root_hash);

      fbl::RefPtr<CacheNode> cache_node;
      if (GetCache().Lookup(digest, &cache_node) != ZX_OK) {
        // Skip blobs that can't be found in the cache.
        continue;
      }
      auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(cache_node));
      if (vnode->DeletionQueued()) {
        // Skip blobs that are scheduled for deletion.
        continue;
      }

      auto name = digest.ToString();
      uint64_t ino = fuchsia_io::wire::kInoUnknown;
      if (df.Next(name, VTYPE_TO_DTYPE(V_TYPE_FILE), ino) != ZX_OK) {
        break;
      }
      c->index = i + 1;
    }
  }

  *out_actual = df.BytesFilled();
  return ZX_OK;
}

zx_status_t Blobfs::BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) {
  zx_status_t status = Device()->BlockAttachVmo(vmo, out);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to attach blob VMO: " << zx_status_get_string(status);
    return status;
  }
  return ZX_OK;
}

zx_status_t Blobfs::BlockDetachVmo(storage::Vmoid vmoid) {
  return Device()->BlockDetachVmo(std::move(vmoid));
}

zx_status_t Blobfs::AddInodes(Allocator* allocator) {
  TRACE_DURATION("blobfs", "Blobfs::AddInodes");

  if (!(info_.flags & kBlobFlagFVM)) {
    return ZX_ERR_NO_SPACE;
  }

  const size_t blocks_per_slice = info_.slice_size / kBlobfsBlockSize;
  uint64_t offset = (kFVMNodeMapStart / blocks_per_slice) + info_.ino_slices;
  uint64_t length = 1;
  zx_status_t status = Device()->VolumeExtend(offset, length);
  bool failed_to_extend = (status != ZX_OK);
  inspect_tree_.UpdateVolumeData(*Device(), failed_to_extend);
  if (failed_to_extend) {
    FX_LOGS(ERROR) << ":AddInodes fvm_extend failure: " << zx_status_get_string(status);
    return status;
  }

  const uint32_t kInodesPerSlice =
      safemath::checked_cast<uint32_t>(info_.slice_size / kBlobfsInodeSize);
  uint64_t inodes64 = (info_.ino_slices + length) * kInodesPerSlice;
  ZX_DEBUG_ASSERT(inodes64 <= std::numeric_limits<uint32_t>::max());
  uint32_t inodes = safemath::checked_cast<uint32_t>(inodes64);
  uint32_t inoblks = (inodes + kBlobfsInodesPerBlock - 1) / kBlobfsInodesPerBlock;
  ZX_DEBUG_ASSERT(info_.inode_count <= std::numeric_limits<uint32_t>::max());
  uint32_t inoblks_old =
      (safemath::checked_cast<uint32_t>(info_.inode_count) + kBlobfsInodesPerBlock - 1) /
      kBlobfsInodesPerBlock;
  ZX_DEBUG_ASSERT(inoblks_old <= inoblks);

  if (allocator->GrowNodeMap(inoblks * kBlobfsBlockSize) != ZX_OK) {
    return ZX_ERR_NO_SPACE;
  }

  info_.ino_slices += safemath::checked_cast<uint32_t>(length);
  info_.inode_count = inodes;

  // Reset new inodes to 0, and update the info block.
  uint64_t zeroed_nodes_blocks = inoblks - inoblks_old;
  // Use GetNode to get a pointer to the first node we need to zero and also to keep the map locked
  // whilst we zero them.
  auto new_nodes = allocator->GetNode(inoblks_old * kBlobfsInodesPerBlock);
  ZX_ASSERT_MSG(new_nodes.is_ok(), "The new nodes should be valid: %s", new_nodes.status_string());
  memset(&*new_nodes.value(), 0, kBlobfsBlockSize * zeroed_nodes_blocks);

  BlobTransaction transaction;
  WriteInfo(transaction);
  if (zeroed_nodes_blocks > 0) {
    transaction.AddOperation({
        .vmo = zx::unowned_vmo(allocator->GetNodeMapVmo().get()),
        .op =
            {
                .type = storage::OperationType::kWrite,
                .vmo_offset = inoblks_old,
                .dev_offset = NodeMapStartBlock(info_) + inoblks_old,
                .length = zeroed_nodes_blocks,
            },
    });
  }
  transaction.Commit(*journal_);
  return ZX_OK;
}

zx_status_t Blobfs::AddBlocks(size_t nblocks, RawBitmap* block_map) {
  TRACE_DURATION("blobfs", "Blobfs::AddBlocks", "nblocks", nblocks);

  if (!(info_.flags & kBlobFlagFVM)) {
    return ZX_ERR_NO_SPACE;
  }

  const size_t blocks_per_slice = info_.slice_size / kBlobfsBlockSize;
  // Number of slices required to add nblocks
  uint64_t offset = (kFVMDataStart / blocks_per_slice) + info_.dat_slices;
  uint64_t length = (nblocks + blocks_per_slice - 1) / blocks_per_slice;

  uint64_t blocks64 = (info_.dat_slices + length) * blocks_per_slice;
  ZX_DEBUG_ASSERT(blocks64 <= std::numeric_limits<uint32_t>::max());
  uint32_t blocks = safemath::checked_cast<uint32_t>(blocks64);
  uint32_t abmblks = (blocks + kBlobfsBlockBits - 1) / kBlobfsBlockBits;
  uint64_t abmblks_old = (info_.data_block_count + kBlobfsBlockBits - 1) / kBlobfsBlockBits;
  ZX_DEBUG_ASSERT(abmblks_old <= abmblks);

  if (abmblks > blocks_per_slice) {
    // TODO(planders): Allocate more slices for the block bitmap.
    FX_LOGS(ERROR) << ":AddBlocks needs to increase block bitmap size";
    return ZX_ERR_NO_SPACE;
  }

  zx_status_t status = Device()->VolumeExtend(offset, length);
  bool failed_to_extend = (status != ZX_OK);
  inspect_tree_.UpdateVolumeData(*Device(), failed_to_extend);
  if (failed_to_extend) {
    FX_LOGS(ERROR) << ":AddBlocks FVM Extend failure: " << zx_status_get_string(status);
    return status;
  }

  // Grow the block bitmap to hold new number of blocks
  if (block_map->Grow(fbl::round_up(blocks, kBlobfsBlockBits)) != ZX_OK) {
    return ZX_ERR_NO_SPACE;
  }
  // Grow before shrinking to ensure the underlying storage is a multiple of kBlobfsBlockSize.
  block_map->Shrink(blocks);

  info_.dat_slices += safemath::checked_cast<uint32_t>(length);
  info_.data_block_count = blocks;

  BlobTransaction transaction;
  WriteInfo(transaction);
  uint64_t zeroed_bitmap_blocks = abmblks - abmblks_old;
  // Since we are extending the bitmap, we need to fill the expanded portion of the allocation block
  // bitmap with zeroes.
  if (zeroed_bitmap_blocks > 0) {
    storage::UnbufferedOperation operation = {
        .vmo = zx::unowned_vmo(block_map->StorageUnsafe()->GetVmo().get()),
        .op =
            {
                .type = storage::OperationType::kWrite,
                .vmo_offset = abmblks_old,
                .dev_offset = BlockMapStartBlock(info_) + abmblks_old,
                .length = zeroed_bitmap_blocks,
            },
    };
    transaction.AddOperation(operation);
  }
  transaction.Commit(*journal_);

  return ZX_OK;
}

zx::status<fs::FilesystemInfo> Blobfs::GetFilesystemInfo() {
  fs::FilesystemInfo info{};

  info.block_size = kBlobfsBlockSize;
  info.max_filename_size = digest::kSha256HexLength;
  info.fs_type = fuchsia_fs::VfsType::kBlobfs;
  // data_block_count and alloc_block_count are 64-bits so this shouldn't overflow unless the
  // header is corrupt.
  info.total_bytes = Info().data_block_count * Info().block_size;
  info.used_bytes = Info().alloc_block_count * Info().block_size;
  info.total_nodes = Info().inode_count;
  info.used_nodes = Info().alloc_inode_count;
  info.SetFsId(fs_id_);
  info.name = "blobfs";

  if (Device()) {
    auto result = fs_inspect::VolumeData::GetSizeInfoFromDevice(*Device());
    if (result.is_ok()) {
      info.free_shared_pool_bytes = result->available_space_bytes;
    }
  }

  return zx::ok(info);
}

zx::status<BlockIterator> Blobfs::BlockIteratorByNodeIndex(uint32_t node_index) {
  auto extent_iter = AllocatedExtentIterator::Create(GetAllocator(), node_index);
  if (extent_iter.is_error()) {
    return extent_iter.take_error();
  }
  return zx::ok(
      BlockIterator(std::make_unique<AllocatedExtentIterator>(std::move(extent_iter.value()))));
}

void Blobfs::Sync(SyncCallback cb) {
  TRACE_DURATION("blobfs", "Blobfs::Sync");
  if (journal_ == nullptr) {
    return cb(ZX_OK);
  }

  auto trace_id = TRACE_NONCE();
  TRACE_FLOW_BEGIN("blobfs", "Blobfs.sync", trace_id);

  journal_->schedule_task(journal_->Sync().then(
      [trace_id, cb = std::move(cb)](fpromise::result<void, zx_status_t>& result) mutable {
        TRACE_DURATION("blobfs", "Blobfs::Sync::callback");

        if (result.is_ok()) {
          cb(ZX_OK);
        } else {
          cb(result.error());
        }

        TRACE_FLOW_END("blobfs", "Blobfs.sync", trace_id);
      }));
}

Blobfs::Blobfs(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device,
               fs::PagedVfs* vfs, const Superblock* info, Writability writable,
               CompressionSettings write_compression_settings, zx::resource vmex_resource,
               std::optional<CachePolicy> pager_backed_cache_policy,
               DecompressorCreatorConnector* decompression_connector, bool use_streaming_writes,
               bool allow_offline_compression)
    : vfs_(vfs),
      info_(*info),
      dispatcher_(dispatcher),
      block_device_(std::move(device)),
      writability_(writable),
      write_compression_settings_(write_compression_settings),
      vmex_resource_(std::move(vmex_resource)),
      inspect_tree_(),
      metrics_(CreateBlobfsMetrics(inspect_tree_.inspector())),
      pager_backed_cache_policy_(pager_backed_cache_policy),
      decompression_connector_(decompression_connector),
      use_streaming_writes_(use_streaming_writes),
      allow_offline_compression_(allow_offline_compression) {
  ZX_ASSERT(vfs_);

  // It's easy to forget to initialize the PagedVfs in tests which will cause mysterious failures
  // later.
  ZX_ASSERT(vfs_->is_initialized());

  zx::event::create(0, &fs_id_);
}

std::unique_ptr<BlockDevice> Blobfs::Reset() {
  // XXX This function relies on very subtle orderings and assumptions about the state of the
  // filesystem. Proceed with caution whenever making changes to Blobfs::Reset(), and consult the
  // blame history for the graveyard of bugs past.
  // TODO(fxbug.dev/56464): simplify the teardown path.
  if (!block_device_) {
    return nullptr;
  }

  FX_LOGS(INFO) << "Shutting down";

  // Shutdown all internal connections to blobfs.
  GetCache().ForAllOpenNodes([](fbl::RefPtr<CacheNode> cache_node) {
    auto blob = fbl::RefPtr<Blob>::Downcast(std::move(cache_node));
    blob->WillTeardownFilesystem();
    return ZX_OK;
  });

  // Write the clean bit.
  if (writability_ == Writability::Writable) {
    // TODO(fxbug.dev/42174): If blobfs initialization failed, it is possible that the info_mapping_
    // vmo that we use to send writes to the underlying block device has not been initialized yet.
    // Change Blobfs::Create ordering to try and get the object into a valid state as soon as
    // possible and reassess what is needed in the destructor.
    if (info_mapping_.start() == nullptr) {
      FX_LOGS(ERROR) << "Cannot write journal clean bit";
    } else {
      BlobTransaction transaction;
      info_.flags |= kBlobFlagClean;
      WriteInfo(transaction);
      transaction.Commit(*journal_);
    }
  }
  // Waits for all pending writeback operations to complete or fail.
  journal_.reset();

  // Reset the PageLoader which owns a VMO that is attached to the block FIFO.
  page_loader_ = nullptr;

  // Reset loader_ which also owns a VMO attached to the block device.
  loader_.reset();

  // Flushes the underlying block device.
  this->Flush();

  BlockDetachVmo(std::move(info_vmoid_));

  return std::move(block_device_);
}

zx_status_t Blobfs::InitializeVnodes() {
  GetCache().Reset();
  CompressionMetrics compression_metrics;
  uint32_t total_allocated = 0;

  for (uint32_t node_index = 0; node_index < info_.inode_count; node_index++) {
    auto inode = GetNode(node_index);
    ZX_ASSERT_MSG(inode.is_ok(), "Failed to get node %u: %s", node_index, inode.status_string());
    // We are not interested in free nodes.
    if (!inode->header.IsAllocated()) {
      continue;
    }
    total_allocated++;

    allocator_->MarkNodeAllocated(node_index);

    // Nothing much to do here if this is not an Inode
    if (inode->header.IsExtentContainer()) {
      continue;
    }

    zx_status_t validation_status =
        AllocatedExtentIterator::VerifyIteration(GetNodeFinder(), node_index, inode.value().get());
    if (validation_status != ZX_OK) {
      // Whatever the more differentiated error is here, the real root issue is the integrity of the
      // data that was just mirrored from the disk.
      FX_LOGS(ERROR) << "failed to validate node @ index " << node_index;
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    fbl::RefPtr<Blob> vnode = fbl::MakeRefCounted<Blob>(this, node_index, *inode.value());

    // This blob is added to the cache, where it will quickly be relocated into the "closed set"
    // once we drop our reference to |vnode|. Although we delay reading any of the contents of the
    // blob from disk until requested, this pre-caching scheme allows us to quickly verify or deny
    // the presence of a blob during blob lookup and creation.
    zx_status_t status = GetCache().Add(vnode);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "CORRUPTED FILESYSTEM: Duplicate node: " << vnode->digest() << " @ index "
                     << node_index - 1;
      return status;
    }

    compression_metrics.Update(*inode);
  }

  if (total_allocated != info_.alloc_inode_count) {
    FX_LOGS(ERROR) << "CORRUPTED FILESYSTEM: Allocated nodes mismatch. Expected:"
                   << info_.alloc_inode_count << ". Found: " << total_allocated;
    return ZX_ERR_IO_OVERRUN;
  }

  // Only update compression stats if the filesystem is in a valid state.
  inspect_tree_.UpdateCompressionMetrics(compression_metrics);

  return ZX_OK;
}

void Blobfs::ComputeBlobFragmentation(uint32_t node_index, Inode& inode,
                                      FragmentationMetrics& fragmentation_metrics,
                                      FragmentationStats* out_stats) {
  if (inode.extent_count == 0) {
    return;
  }

  fragmentation_metrics.extents_per_file.Insert(inode.extent_count);
  if (out_stats) {
    ++out_stats->extents_per_file[inode.extent_count];
  }

  for (size_t i = 0; i < std::min<size_t>(kInlineMaxExtents, inode.extent_count); ++i) {
    fragmentation_metrics.in_use_fragments.Insert(inode.extents[i].Length());
    if (out_stats) {
      ++out_stats->in_use_fragments[inode.extents[i].Length()];
    }
  }

  AllocatedNodeIterator extents_iter(GetNodeFinder(), node_index, &inode);
  while (!extents_iter.Done()) {
    zx::status<ExtentContainer*> container_or = extents_iter.Next();
    if (container_or.is_error()) {
      FX_LOGS(ERROR) << "Failed to get next extent container for inode " << node_index << ": "
                     << container_or.status_string();
      // Attempt to continue onto the next extent if we fail on this one.
      continue;
    }
    auto container = container_or.value();
    for (size_t i = 0; i < container->extent_count; ++i) {
      fragmentation_metrics.in_use_fragments.Insert(container->extents[i].Length());
      if (out_stats) {
        ++out_stats->in_use_fragments[container->extents[i].Length()];
      }
    }
  }
}

void Blobfs::CalculateFragmentationMetrics(FragmentationMetrics& fragmentation_metrics,
                                           FragmentationStats* out_stats) {
  TRACE_DURATION("blobfs", "Blobfs::CalculateFragmentationMetrics");
  if (out_stats) {
    *out_stats = {};
  }

  // Calculate blob-level fragmentation statistics.
  uint64_t extent_containers_in_use = 0;
  uint64_t blobs_in_use = 0;
  for (uint32_t node_index = 0; node_index < info_.inode_count; ++node_index) {
    auto inode = GetNode(node_index);
    if (!inode->header.IsAllocated()) {
      continue;
    }

    if (inode->header.IsExtentContainer()) {
      ++extent_containers_in_use;
      continue;
    }

    ++blobs_in_use;
    ComputeBlobFragmentation(node_index, *inode.value(), fragmentation_metrics, out_stats);
  }

  fragmentation_metrics.total_nodes.Set(Info().inode_count);
  fragmentation_metrics.files_in_use.Set(blobs_in_use);
  fragmentation_metrics.extent_containers_in_use.Set(extent_containers_in_use);

  if (out_stats) {
    out_stats->total_nodes = Info().inode_count;
    out_stats->files_in_use = blobs_in_use;
    out_stats->extent_containers_in_use = extent_containers_in_use;
  }

  // Calculate free space fragmentation.
  uint64_t free_run = 0;
  for (uint64_t i = 0; i < Info().data_block_count; ++i) {
    if (allocator_->IsBlockAllocated(i).value()) {
      // This is the end of free fragment. Count it.
      if (free_run != 0) {
        fragmentation_metrics.free_fragments.Insert(free_run);
        if (out_stats) {
          ++out_stats->free_fragments[free_run];
        }
        free_run = 0;
      }
      continue;
    }
    ++free_run;
  }

  // If this is the end of last free fragment, count it.
  if (free_run != 0) {
    fragmentation_metrics.free_fragments.Insert(free_run);
    if (out_stats) {
      ++out_stats->free_fragments[free_run];
    }
  }
}

zx_status_t Blobfs::ReloadSuperblock() {
  TRACE_DURATION("blobfs", "Blobfs::ReloadSuperblock");

  // Re-read the info block from disk.
  char block[kBlobfsBlockSize];
  block_client::Reader reader(*Device());
  if (zx_status_t status = reader.Read(0, kBlobfsBlockSize, block); status != ZX_OK) {
    FX_LOGS(ERROR) << "could not read info block";
    return status;
  }

  Superblock* info = reinterpret_cast<Superblock*>(block);
  if (zx_status_t status = CheckSuperblock(info, TotalBlocks(*info)); status != ZX_OK) {
    FX_LOGS(ERROR) << "Check info failure";
    return status;
  }

  // Once it has been verified, overwrite the current info.
  memcpy(&info_, info, sizeof(Superblock));
  return ZX_OK;
}

zx_status_t Blobfs::OpenRootNode(fbl::RefPtr<fs::Vnode>* out) {
  fbl::RefPtr<Directory> vn = fbl::AdoptRef(new Directory(this));

  auto validated_options = vn->ValidateOptions(fs::VnodeConnectionOptions());
  if (validated_options.is_error()) {
    return validated_options.status_value();
  }
  zx_status_t status = vn->Open(validated_options.value(), nullptr);
  if (status != ZX_OK) {
    return status;
  }

  *out = std::move(vn);
  return ZX_OK;
}

void Blobfs::FsckAtEndOfTransaction() {
  std::scoped_lock lock(fsck_at_end_of_transaction_mutex_);
  auto device = std::make_unique<block_client::PassThroughReadOnlyBlockDevice>(block_device_.get());
  MountOptions options;
  options.writability = Writability::ReadOnlyDisk;
  ZX_ASSERT(Fsck(std::move(device), options) == ZX_OK);
}

zx_status_t Blobfs::Migrate() {
  if (info_.oldest_minor_version < kBlobfsMinorVersionNoOldCompressionFormats)
    return ZX_ERR_NOT_SUPPORTED;  // Too old to support migration.
  return MigrateToRev4();
}

zx_status_t Blobfs::MigrateToRev4() {
  if (writability_ != Writability::Writable ||
      info_.oldest_minor_version != kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly - 1) {
    return ZX_OK;
  }
  FX_LOGS(INFO) << "Migrating to revision " << kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly;
  BlobTransaction transaction;
  for (uint32_t node_index = 0; node_index < info_.inode_count; ++node_index) {
    auto inode = GetNode(node_index);
    ZX_ASSERT_MSG(inode.is_ok(), "Failed to get node %u: %s", node_index, inode.status_string());
    if (!inode->header.IsAllocated() || inode->header.IsExtentContainer()) {
      continue;
    }
    if (inode->block_count > 0 || inode->extent_count == 0) {
      // The inode isn't the null blob, or it already has a correct extent_count (0)
      continue;
    }
    FX_LOGS(INFO) << "Repairing zero-length extent at index " << node_index;
    inode->extent_count = 0;
    WriteNode(node_index, transaction);
  }
  info_.oldest_minor_version = kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly;
  WriteInfo(transaction);
  transaction.Commit(*journal_);
  return ZX_OK;
}

zx_status_t Blobfs::RunRequests(const std::vector<storage::BufferedOperation>& operations) {
  std::shared_lock lock(fsck_at_end_of_transaction_mutex_);
  return TransactionManager::RunRequests(operations);
}

zx::status<std::unique_ptr<Superblock>> Blobfs::ReadBackupSuperblock() {
  // If the filesystem is writable, it's possible that we just wrote a backup superblock, so issue
  // a sync just in case.
  if (writability_ == Writability::Writable) {
    sync_completion_t sync;
    Sync([&](zx_status_t status) { sync_completion_signal(&sync); });
    sync_completion_wait(&sync, ZX_TIME_INFINITE);
  }
  auto superblock = std::make_unique<Superblock>();
  block_client::Reader reader(*block_device_);
  if (zx_status_t status = reader.Read(kFVMBackupSuperblockOffset * kBlobfsBlockSize,
                                       kBlobfsBlockSize, superblock.get());
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(superblock));
}

}  // namespace blobfs
