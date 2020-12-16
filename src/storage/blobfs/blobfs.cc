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
#include <lib/zircon-internal/debug.h>
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
#include <utility>

#include <block-client/cpp/pass-through-read-only-device.h>
#include <block-client/cpp/remote-block-device.h>
#include <cobalt-client/cpp/collector.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>
#include <fs/journal/journal.h>
#include <fs/journal/replay.h>
#include <fs/journal/superblock.h>
#include <fs/pseudo_dir.h>
#include <fs/ticker.h>
#include <fs/vfs_types.h>

#include "src/storage/blobfs/allocator/extent-reserver.h"
#include "src/storage/blobfs/allocator/node-reserver.h"
#include "src/storage/blobfs/blob-loader.h"
#include "src/storage/blobfs/blob.h"
#include "src/storage/blobfs/blobfs-checker.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/compression-settings.h"
#include "src/storage/blobfs/compression/compressor.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/fsck.h"
#include "src/storage/blobfs/iterator/allocated-extent-iterator.h"
#include "src/storage/blobfs/iterator/allocated-node-iterator.h"
#include "src/storage/blobfs/iterator/block-iterator.h"
#include "src/storage/blobfs/pager/transfer-buffer.h"
#include "src/storage/blobfs/pager/user-pager-info.h"
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
  zx_status_t status = device.ReadBlock(block_offset * kBlobfsBlockSize / block_info.block_size,
                                        kBlobfsBlockSize, block);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "could not read info block";
    return status;
  }
  const Superblock* superblock = reinterpret_cast<Superblock*>(&block[0]);

  uint64_t blocks = (block_info.block_size * block_info.block_count) / kBlobfsBlockSize;
  if (kBlobfsBlockSize % block_info.block_size != 0) {
    FX_LOGS(ERROR) << "Blobfs block size (" << kBlobfsBlockSize
                   << ") not divisible by device block size (" << block_info.block_size << ")";
    return ZX_ERR_IO;
  }

  // Perform superblock validations which should succeed prior to journal replay.
  const uint64_t total_blocks = TotalBlocks(*superblock);
  if (blocks < total_blocks) {
    return ZX_ERR_BAD_STATE;
  }
  return CheckSuperblock(superblock, total_blocks, /*quiet=*/true);
}

}  // namespace

zx_status_t Blobfs::Create(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device,
                           const MountOptions& options, zx::resource vmex_resource,
                           std::unique_ptr<Blobfs>* out) {
  TRACE_DURATION("blobfs", "Blobfs::Create");

  fuchsia_hardware_block_BlockInfo block_info;
  if (zx_status_t status = device->BlockGetInfo(&block_info); status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot acquire block info: " << status;
    return status;
  }

  if (block_info.flags & BLOCK_FLAG_READONLY &&
      (options.writability != blobfs::Writability::ReadOnlyDisk)) {
    return ZX_ERR_ACCESS_DENIED;
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
      return status1;  // Return the first error we found.
    }
    // Backup superblocks are only valid with FVM.
    fvm_required = true;
  }
  const Superblock* superblock = reinterpret_cast<Superblock*>(&block[0]);

  // Construct the Blobfs object, without intensive validation, since it
  // may require upgrades / journal replays to become valid.
  auto fs = std::unique_ptr<Blobfs>(new Blobfs(
      dispatcher, std::move(device), superblock, options.writability, options.compression_settings,
      std::move(vmex_resource), options.pager_backed_cache_policy));
  fs->block_info_ = block_info;

  auto fs_ptr = fs.get();
  auto status_or_uncompressed_buffer = pager::StorageBackedTransferBuffer::Create(
      pager::kTransferBufferSize, fs_ptr, fs_ptr, fs_ptr->Metrics());
  if (!status_or_uncompressed_buffer.is_ok()) {
    FX_LOGS(ERROR) << "Could not initialize uncompressed pager transfer buffer";
    return status_or_uncompressed_buffer.status_value();
  }
  auto status_or_compressed_buffer = pager::StorageBackedTransferBuffer::Create(
      pager::kTransferBufferSize, fs_ptr, fs_ptr, fs_ptr->Metrics());
  if (!status_or_compressed_buffer.is_ok()) {
    FX_LOGS(ERROR) << "Could not initialize compressed pager transfer buffer";
    return status_or_compressed_buffer.status_value();
  }
  auto status_or_pager = pager::UserPager::Create(std::move(status_or_uncompressed_buffer).value(),
                                                  std::move(status_or_compressed_buffer).value(),
                                                  pager::kDecompressionBufferSize,
                                                  fs_ptr->Metrics(), options.sandbox_decompression);
  if (!status_or_pager.is_ok()) {
    FX_LOGS(ERROR) << "Could not initialize user pager";
    return status_or_pager.status_value();
  }
  fs->pager_ = std::move(status_or_pager).value();
  FX_LOGS(INFO) << "Initialized user pager";

  if (options.metrics) {
    fs->metrics_->Collect();
  }

  JournalSuperblock journal_superblock;
  if (options.writability != blobfs::Writability::ReadOnlyDisk) {
    FX_LOGS(INFO) << "Replaying journal";
    auto journal_superblock_or = fs::ReplayJournal(fs.get(), fs.get(), JournalStartBlock(fs->info_),
                                                   JournalBlocks(fs->info_), kBlobfsBlockSize);
    if (journal_superblock_or.is_error()) {
      FX_LOGS(ERROR) << "Failed to replay journal";
      return journal_superblock_or.error_value();
    }
    journal_superblock = std::move(journal_superblock_or.value());
    FX_LOGS(DEBUG) << "Journal replayed";
    if (zx_status_t status = fs->ReloadSuperblock(); status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to re-load superblock";
      return status;
    }
  }

  if (fvm_required && (fs->Info().flags & kBlobFlagFVM) == 0) {
    FX_LOGS(ERROR) << "FVM required but superblock indicates otherwise";
    return ZX_ERR_INVALID_ARGS;
  }

  switch (options.writability) {
    case blobfs::Writability::Writable: {
      FX_LOGS(DEBUG) << "Initializing journal for writeback";
      auto journal_or =
          InitializeJournal(fs.get(), fs.get(), JournalStartBlock(fs->info_),
                            JournalBlocks(fs->info_), std::move(journal_superblock), fs->metrics_);
      if (journal_or.is_error()) {
        FX_LOGS(ERROR) << "Failed to initialize journal";
        return journal_or.error_value();
      }
      fs->journal_ = std::move(journal_or.value());
#ifndef NDEBUG
      if (options.fsck_at_end_of_every_transaction) {
        fs->journal_->set_write_metadata_callback(
            fit::bind_member(fs.get(), &Blobfs::FsckAtEndOfTransaction));
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
    return status;
  }

  FX_LOGS(INFO) << "Using eviction policy " << CachePolicyToString(options.cache_policy);
  if (options.pager_backed_cache_policy) {
    FX_LOGS(INFO) << "Using overridden pager eviction policy "
                  << CachePolicyToString(*options.pager_backed_cache_policy);
  }
  fs->Cache().SetCachePolicy(options.cache_policy);

  RawBitmap block_map;
  // Keep the block_map aligned to a block multiple
  if ((status = block_map.Reset(BlockMapBlocks(fs->info_) * kBlobfsBlockBits)) < 0) {
    FX_LOGS(ERROR) << "Could not reset block bitmap";
    return status;
  }
  if ((status = block_map.Shrink(fs->info_.data_block_count)) < 0) {
    FX_LOGS(ERROR) << "Could not shrink block bitmap";
    return status;
  }
  fzl::ResizeableVmoMapper node_map;

  size_t nodemap_size = kBlobfsInodeSize * fs->info_.inode_count;
  ZX_DEBUG_ASSERT(fbl::round_up(nodemap_size, kBlobfsBlockSize) == nodemap_size);
  ZX_DEBUG_ASSERT(nodemap_size / kBlobfsBlockSize == NodeMapBlocks(fs->info_));
  if ((status = node_map.CreateAndMap(nodemap_size, "nodemap")) != ZX_OK) {
    return status;
  }
  std::unique_ptr<IdAllocator> nodes_bitmap = {};
  if ((status = IdAllocator::Create(fs->info_.inode_count, &nodes_bitmap) != ZX_OK)) {
    FX_LOGS(ERROR) << "Failed to allocate bitmap for inodes";
    return status;
  }

  fs->allocator_ = std::make_unique<Allocator>(fs.get(), std::move(block_map), std::move(node_map),
                                               std::move(nodes_bitmap));
  if ((status = fs->allocator_->ResetFromStorage(fs::ReadTxn(fs.get()))) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to load bitmaps: " << status;
    return status;
  }
  if ((status = fs->info_mapping_.CreateAndMap(kBlobfsBlockSize, "blobfs-superblock")) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create info vmo: " << status;
    return status;
  }
  if ((status = fs->BlockAttachVmo(fs->info_mapping_.vmo(), &fs->info_vmoid_)) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to attach info vmo: " << status;
    return status;
  }
  if ((status = fs->CreateFsId()) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create fs_id: " << status;
    return status;
  }
  if ((status = fs->InitializeVnodes()) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize Vnodes";
    return status;
  }
  zx::status<BlobLoader> loader =
      BlobLoader::Create(fs_ptr, fs_ptr, fs->GetNodeFinder(), fs->pager_.get(), fs->Metrics(),
                         options.sandbox_decompression);
  if (!loader.is_ok()) {
    FX_LOGS(ERROR) << "Failed to initialize loader: " << loader.status_string();
    return loader.status_value();
  }
  fs->loader_ = std::move(loader.value());

  // At this point, the filesystem is loaded and validated. No errors should be returned after this
  // point.

  // On a read-write filesystem, since we can now serve writes, we need to unset the kBlobFlagClean
  // flag to indicate that the filesystem may not be in a "clean" state anymore. This helps to make
  // sure we are unmounted cleanly i.e the kBlobFlagClean flag is set back on clean unmount.
  //
  // Additionally, we can now update the oldest_revision field if it needs to be updated.
  FX_LOGS(INFO) << "detected oldest_revision " << fs->info_.oldest_revision << ", current revision "
                << kBlobfsCurrentRevision;
  if (options.writability == blobfs::Writability::Writable) {
    BlobTransaction transaction;
    fs->info_.flags &= ~kBlobFlagClean;
    if (fs->info_.oldest_revision > kBlobfsCurrentRevision) {
      FX_LOGS(INFO) << "Setting oldest_revision to " << kBlobfsCurrentRevision;
      fs->info_.oldest_revision = kBlobfsCurrentRevision;
    }
    // Write a backup superblock if there's an old version of blobfs.
    bool write_backup = false;
    if (fs->info_.oldest_revision < kBlobfsRevisionBackupSuperblock) {
      FX_LOGS(INFO) << "Upgrading to latest revision";
      if (fs->Info().flags & kBlobFlagFVM) {
        FX_LOGS(INFO) << "Writing backup superblock";
        write_backup = true;
      }
      fs->info_.oldest_revision = kBlobfsRevisionBackupSuperblock;
    }
    fs->WriteInfo(transaction, write_backup);
    transaction.Commit(*fs->journal());
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

  status = BlobCorruptionNotifier::Create(&(fs->blob_corruption_notifier_));

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize corruption notifier: " << zx_status_get_string(status);
  }

  *out = std::move(fs);
  return ZX_OK;
}

// Writeback enabled, journaling enabled.
zx::status<std::unique_ptr<Journal>> Blobfs::InitializeJournal(
    fs::TransactionHandler* transaction_handler, VmoidRegistry* registry, uint64_t journal_start,
    uint64_t journal_length, JournalSuperblock journal_superblock,
    std::shared_ptr<fs::MetricsTrait> journal_metrics) {
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

  auto options = Journal::Options();
  options.metrics = journal_metrics;
  return zx::ok(std::make_unique<Journal>(transaction_handler, std::move(journal_superblock),
                                          std::move(journal_buffer), std::move(writeback_buffer),
                                          journal_start, options));
}

std::unique_ptr<BlockDevice> Blobfs::Destroy(std::unique_ptr<Blobfs> blobfs) {
  return blobfs->Reset();
}

Blobfs::~Blobfs() { Reset(); }

zx_status_t Blobfs::LoadAndVerifyBlob(uint32_t node_index) {
  return Blob::LoadAndVerifyBlob(this, node_index);
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
    // Always write back the first node.
    if (zx_status_t status = FreeNode(node_index, transaction); status != ZX_OK) {
      return status;
    }

    auto extent_iter = AllocatedExtentIterator::Create(allocator_.get(), node_index);
    if (extent_iter.is_error()) {
      return extent_iter.status_value();
    }
    while (!extent_iter->Done()) {
      // If we're observing a new node, free it.
      if (extent_iter->NodeIndex() != node_index) {
        node_index = extent_iter->NodeIndex();
        if (zx_status_t status = FreeNode(node_index, transaction); status != ZX_OK) {
          return status;
        }
      }

      const Extent* extent;
      ZX_ASSERT(extent_iter->Next(&extent) == ZX_OK);

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

zx_status_t Blobfs::CreateFsId() {
  ZX_DEBUG_ASSERT(!fs_id_legacy_);
  ZX_DEBUG_ASSERT(!fs_id_.is_valid());
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  if (status != ZX_OK) {
    return status;
  }
  zx_info_handle_basic_t info;
  status = event.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  fs_id_ = std::move(event);
  fs_id_legacy_ = info.koid;
  return ZX_OK;
}

zx_status_t Blobfs::GetFsId(zx::event* out_fs_id) const {
  ZX_DEBUG_ASSERT(fs_id_.is_valid());
  return fs_id_.duplicate(ZX_RIGHTS_BASIC, out_fs_id);
}

static_assert(sizeof(DirectoryCookie) <= sizeof(fs::vdircookie_t),
              "Blobfs dircookie too large to fit in IO state");

zx_status_t Blobfs::Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                            size_t* out_actual) {
  TRACE_DURATION("blobfs", "Blobfs::Readdir", "len", len);
  fs::DirentFiller df(dirents, len);
  DirectoryCookie* c = reinterpret_cast<DirectoryCookie*>(cookie);

  for (size_t i = c->index; i < info_.inode_count; ++i) {
    ZX_DEBUG_ASSERT(i < std::numeric_limits<uint32_t>::max());
    uint32_t node_index = static_cast<uint32_t>(i);
    if (GetNode(node_index)->header.IsAllocated() &&
        !GetNode(node_index)->header.IsExtentContainer()) {
      Digest digest(GetNode(node_index)->merkle_root_hash);
      auto name = digest.ToString();
      uint64_t ino = ::llcpp::fuchsia::io::INO_UNKNOWN;
      if (df.Next(name.ToStringPiece(), VTYPE_TO_DTYPE(V_TYPE_FILE), ino) != ZX_OK) {
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
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << ":AddInodes fvm_extend failure: " << zx_status_get_string(status);
    return status;
  }

  const uint32_t kInodesPerSlice = static_cast<uint32_t>(info_.slice_size / kBlobfsInodeSize);
  uint64_t inodes64 = (info_.ino_slices + static_cast<uint32_t>(length)) * kInodesPerSlice;
  ZX_DEBUG_ASSERT(inodes64 <= std::numeric_limits<uint32_t>::max());
  uint32_t inodes = static_cast<uint32_t>(inodes64);
  uint32_t inoblks = (inodes + kBlobfsInodesPerBlock - 1) / kBlobfsInodesPerBlock;
  ZX_DEBUG_ASSERT(info_.inode_count <= std::numeric_limits<uint32_t>::max());
  uint32_t inoblks_old = (static_cast<uint32_t>(info_.inode_count) + kBlobfsInodesPerBlock - 1) /
                         kBlobfsInodesPerBlock;
  ZX_DEBUG_ASSERT(inoblks_old <= inoblks);

  if (allocator->GrowNodeMap(inoblks * kBlobfsBlockSize) != ZX_OK) {
    return ZX_ERR_NO_SPACE;
  }

  info_.ino_slices += static_cast<uint32_t>(length);
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
  uint32_t blocks = static_cast<uint32_t>(blocks64);
  uint32_t abmblks = (blocks + kBlobfsBlockBits - 1) / kBlobfsBlockBits;
  uint64_t abmblks_old = (info_.data_block_count + kBlobfsBlockBits - 1) / kBlobfsBlockBits;
  ZX_DEBUG_ASSERT(abmblks_old <= abmblks);

  if (abmblks > blocks_per_slice) {
    // TODO(planders): Allocate more slices for the block bitmap.
    FX_LOGS(ERROR) << ":AddBlocks needs to increase block bitmap size";
    return ZX_ERR_NO_SPACE;
  }

  zx_status_t status = Device()->VolumeExtend(offset, length);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << ":AddBlocks FVM Extend failure: " << zx_status_get_string(status);
    return status;
  }

  // Grow the block bitmap to hold new number of blocks
  if (block_map->Grow(fbl::round_up(blocks, kBlobfsBlockBits)) != ZX_OK) {
    return ZX_ERR_NO_SPACE;
  }
  // Grow before shrinking to ensure the underlying storage is a multiple
  // of kBlobfsBlockSize.
  block_map->Shrink(blocks);

  info_.dat_slices += static_cast<uint32_t>(length);
  info_.data_block_count = blocks;

  BlobTransaction transaction;
  WriteInfo(transaction);
  uint64_t zeroed_bitmap_blocks = abmblks - abmblks_old;
  // Since we are extending the bitmap, we need to fill the expanded
  // portion of the allocation block bitmap with zeroes.
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

constexpr const char kFsName[] = "blobfs";
void Blobfs::GetFilesystemInfo(FilesystemInfo* info) const {
  static_assert(fbl::constexpr_strlen(kFsName) + 1 < ::llcpp::fuchsia::io::MAX_FS_NAME_BUFFER,
                "Blobfs name too long");

  *info = {};
  info->block_size = kBlobfsBlockSize;
  info->max_filename_size = digest::kSha256HexLength;
  info->fs_type = VFS_TYPE_BLOBFS;
  info->fs_id = GetFsIdLegacy();
  info->total_bytes = Info().data_block_count * Info().block_size;
  info->used_bytes = Info().alloc_block_count * Info().block_size;
  info->total_nodes = Info().inode_count;
  info->used_nodes = Info().alloc_inode_count;
  strlcpy(reinterpret_cast<char*>(info->name.data()), kFsName,
          ::llcpp::fuchsia::io::MAX_FS_NAME_BUFFER);
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
      [trace_id, cb = std::move(cb)](fit::result<void, zx_status_t>& result) mutable {
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
               const Superblock* info, Writability writable,
               CompressionSettings write_compression_settings, zx::resource vmex_resource,
               std::optional<CachePolicy> pager_backed_cache_policy)
    : info_(*info),
      dispatcher_(dispatcher),
      block_device_(std::move(device)),
      writability_(writable),
      write_compression_settings_(write_compression_settings),
      vmex_resource_(std::move(vmex_resource)),
      pager_backed_cache_policy_(pager_backed_cache_policy) {}

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
  Cache().ForAllOpenNodes([](fbl::RefPtr<CacheNode> cache_node) {
    auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(cache_node));
    vnode->CloneWatcherTeardown();
  });

  // Write the clean bit.
  if (writability_ == Writability::Writable) {
    // TODO(fxbug.dev/42174): If blobfs initialization failed, it is possible that the
    // info_mapping_ vmo that we use to send writes to the underlying block device
    // has not been initialized yet. Change Blobfs::Create ordering to try and get
    // the object into a valid state as soon as possible and reassess what is needed
    // in the destructor.
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

  // Reset |pager_| which owns a VMO that is attached to the block FIFO.
  pager_ = nullptr;

  // Flushes the underlying block device.
  fs::WriteTxn sync_txn(this);
  sync_txn.EnqueueFlush();
  sync_txn.Transact();

  BlockDetachVmo(std::move(info_vmoid_));

  return std::move(block_device_);
}

zx_status_t Blobfs::InitializeVnodes() {
  Cache().Reset();
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
        AllocatedExtentIterator::VerifyIteration(GetNodeFinder(), inode.value().get());
    if (validation_status != ZX_OK) {
      // Whatever the more differentiated error is here, the real root issue is
      // the integrity of the data that was just mirrored from the disk.
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    fbl::RefPtr<Blob> vnode = fbl::MakeRefCounted<Blob>(this, node_index, *inode.value());

    // This blob is added to the cache, where it will quickly be relocated into the "closed
    // set" once we drop our reference to |vnode|. Although we delay reading any of the
    // contents of the blob from disk until requested, this pre-caching scheme allows us to
    // quickly verify or deny the presence of a blob during blob lookup and creation.
    zx_status_t status = Cache().Add(vnode);
    if (status != ZX_OK) {
      Digest digest(vnode->GetNode().merkle_root_hash);
      FX_LOGS(ERROR) << "CORRUPTED FILESYSTEM: Duplicate node: " << digest.ToString() << " @ index "
                     << node_index - 1;
      return status;
    }
    metrics_->IncrementCompressionFormatMetric(*inode.value());
  }

  if (total_allocated != info_.alloc_inode_count) {
    FX_LOGS(ERROR) << "CORRUPTED FILESYSTEM: Allocated nodes mismatch. Expected:"
                   << info_.alloc_inode_count << ". Found: " << total_allocated;
    return ZX_ERR_IO_OVERRUN;
  }

  return ZX_OK;
}

zx_status_t Blobfs::ReloadSuperblock() {
  TRACE_DURATION("blobfs", "Blobfs::ReloadSuperblock");

  // Re-read the info block from disk.
  char block[kBlobfsBlockSize];
  if (zx_status_t status = Device()->ReadBlock(0, kBlobfsBlockSize, block); status != ZX_OK) {
    FX_LOGS(ERROR) << "could not read info block";
    return status;
  }

  Superblock* info = reinterpret_cast<Superblock*>(&block[0]);
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
    return validated_options.error();
  }
  zx_status_t status = vn->Open(validated_options.value(), nullptr);
  if (status != ZX_OK) {
    return status;
  }

  *out = std::move(vn);
  return ZX_OK;
}

Journal* Blobfs::journal() { return journal_.get(); }

void Blobfs::FsckAtEndOfTransaction() {
  std::scoped_lock lock(fsck_at_end_of_transaction_mutex_);
  auto device = std::make_unique<block_client::PassThroughReadOnlyBlockDevice>(block_device_.get());
  MountOptions options;
  options.writability = Writability::ReadOnlyDisk;
  ZX_ASSERT(Fsck(std::move(device), options) == ZX_OK);
}

zx_status_t Blobfs::RunRequests(const std::vector<storage::BufferedOperation>& operations) {
  std::shared_lock lock(fsck_at_end_of_transaction_mutex_);
  return TransactionManager::RunRequests(operations);
}

std::shared_ptr<BlobfsMetrics> Blobfs::CreateMetrics() {
  bool enable_page_in_metrics = false;
#ifdef BLOBFS_ENABLE_PAGE_IN_METRICS
  enable_page_in_metrics = true;
#endif
  return std::make_shared<BlobfsMetrics>(enable_page_in_metrics);
}

zx::status<std::unique_ptr<Superblock>> Blobfs::ReadBackupSuperblock() {
  // If the filesystem is writable, it's possible that we just wrote a backup superblock, so issue a
  // sync just in case.
  if (writability_ == Writability::Writable) {
    sync_completion_t sync;
    Sync([&](zx_status_t status) { sync_completion_signal(&sync); });
    sync_completion_wait(&sync, ZX_TIME_INFINITE);
  }
  auto superblock = std::make_unique<Superblock>();
  if (zx_status_t status =
          block_device_->ReadBlock(kFVMBackupSuperblockOffset, kBlobfsBlockSize, superblock.get());
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(superblock));
}

}  // namespace blobfs
