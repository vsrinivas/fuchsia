// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <inttypes.h>
#include <lib/async/cpp/task.h>
#include <lib/cksum.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/debug.h>
#include <lib/zx/event.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <limits>

#include <blobfs/blobfs.h>
#include <blobfs/compression/compressor.h>
#include <blobfs/extent-reserver.h>
#include <blobfs/fsck.h>
#include <blobfs/node-reserver.h>
#include <cobalt-client/cpp/collector.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>
#include <fs/journal/replay.h>
#include <fs/journal/superblock.h>
#include <fs/ticker.h>
#include <fs/transaction/block_transaction.h>
#include <fvm/client.h>

#define ZXDEBUG 0

#include <utility>

using block_client::RemoteBlockDevice;
using digest::Digest;
using digest::MerkleTree;
using fs::BlockingRingBuffer;
using fs::Journal;
using fs::JournalSuperblock;
using fs::VmoidRegistry;
using id_allocator::IdAllocator;

namespace blobfs {
namespace {

// Time between each Cobalt flush.
constexpr zx::duration kCobaltFlushTimer = zx::min(5);

// Writeback enabled, journaling enabled.
zx_status_t InitializeJournal(fs::TransactionHandler* transaction_handler, VmoidRegistry* registry,
                              uint64_t journal_start, uint64_t journal_length,
                              JournalSuperblock journal_superblock,
                              std::unique_ptr<Journal>* out_journal) {
  const uint64_t journal_entry_blocks = journal_length - fs::kJournalMetadataBlocks;

  std::unique_ptr<BlockingRingBuffer> journal_buffer;
  zx_status_t status = BlockingRingBuffer::Create(registry, journal_entry_blocks, kBlobfsBlockSize,
                                                  "journal-writeback-buffer", &journal_buffer);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot create journal buffer: %s\n", zx_status_get_string(status));
    return status;
  }

  std::unique_ptr<BlockingRingBuffer> writeback_buffer;
  status = BlockingRingBuffer::Create(registry, WriteBufferSize(), kBlobfsBlockSize,
                                      "data-writeback-buffer", &writeback_buffer);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot create writeback buffer: %s\n", zx_status_get_string(status));
    return status;
  }

  *out_journal = std::make_unique<Journal>(transaction_handler, std::move(journal_superblock),
                                           std::move(journal_buffer), std::move(writeback_buffer),
                                           journal_start);
  return ZX_OK;
}

// Writeback enabled, journaling disabled.
zx_status_t InitializeUnjournalledWriteback(fs::TransactionHandler* transaction_handler,
                                            VmoidRegistry* registry,
                                            std::unique_ptr<Journal>* out_journal) {
  std::unique_ptr<BlockingRingBuffer> writeback_buffer;
  zx_status_t status = BlockingRingBuffer::Create(registry, WriteBufferSize(), kBlobfsBlockSize,
                                                  "data-writeback-buffer", &writeback_buffer);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot create writeback buffer: %s\n", zx_status_get_string(status));
    return status;
  }

  *out_journal = std::make_unique<Journal>(transaction_handler, std::move(writeback_buffer));
  return ZX_OK;
}

}  // namespace

zx_status_t Blobfs::VerifyBlob(uint32_t node_index) { return Blob::VerifyBlob(this, node_index); }

void Blobfs::PersistBlocks(const ReservedExtent& reserved_extent,
                           fs::UnbufferedOperationsBuilder* operations) {
  TRACE_DURATION("blobfs", "Blobfs::PersistBlocks");

  allocator_->MarkBlocksAllocated(reserved_extent);

  const Extent& extent = reserved_extent.extent();
  info_.alloc_block_count += extent.Length();
  // Write out to disk.
  WriteBitmap(extent.Length(), extent.Start(), operations);
  WriteInfo(operations);
}

// Frees blocks from reserved and allocated maps, updates disk in the latter case.
void Blobfs::FreeExtent(const Extent& extent, fs::UnbufferedOperationsBuilder* operations) {
  size_t start = extent.Start();
  size_t num_blocks = extent.Length();
  size_t end = start + num_blocks;

  TRACE_DURATION("blobfs", "Blobfs::FreeExtent", "nblocks", num_blocks, "blkno", start);

  // Check if blocks were allocated on disk.
  if (allocator_->CheckBlocksAllocated(start, end)) {
    allocator_->FreeBlocks(extent);
    info_.alloc_block_count -= num_blocks;
    WriteBitmap(num_blocks, start, operations);
    WriteInfo(operations);
  }
}

void Blobfs::FreeNode(uint32_t node_index, fs::UnbufferedOperationsBuilder* operations) {
  allocator_->FreeNode(node_index);
  info_.alloc_inode_count--;
  WriteNode(node_index, operations);
}

void Blobfs::FreeInode(uint32_t node_index, fs::UnbufferedOperationsBuilder* operations) {
  TRACE_DURATION("blobfs", "Blobfs::FreeInode", "node_index", node_index);
  Inode* mapped_inode = GetNode(node_index);
  ZX_DEBUG_ASSERT(operations != nullptr);

  if (mapped_inode->header.IsAllocated()) {
    // Always write back the first node.
    FreeNode(node_index, operations);

    AllocatedExtentIterator extent_iter(allocator_.get(), node_index);
    while (!extent_iter.Done()) {
      // If we're observing a new node, free it.
      if (extent_iter.NodeIndex() != node_index) {
        node_index = extent_iter.NodeIndex();
        FreeNode(node_index, operations);
      }

      const Extent* extent;
      ZX_ASSERT(extent_iter.Next(&extent) == ZX_OK);

      // Free the extent.
      FreeExtent(*extent, operations);
    }
    WriteInfo(operations);
  }
}

void Blobfs::PersistNode(uint32_t node_index, fs::UnbufferedOperationsBuilder* operations) {
  TRACE_DURATION("blobfs", "Blobfs::PersistNode");
  info_.alloc_inode_count++;
  WriteNode(node_index, operations);
  WriteInfo(operations);
}

size_t Blobfs::WritebackCapacity() const { return WriteBufferSize(); }

void Blobfs::Shutdown(fs::Vfs::ShutdownCallback cb) {
  TRACE_DURATION("blobfs", "Blobfs::Unmount");

  // 1) Shutdown all external connections to blobfs.
  ManagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    // 2a) Shutdown all internal connections to blobfs.
    Cache().ForAllOpenNodes([](fbl::RefPtr<CacheNode> cache_node) {
      auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(cache_node));
      vnode->CloneWatcherTeardown();
    });

    // 2b) Flush all pending work to blobfs to the underlying storage.
    Sync([this, cb = std::move(cb)](zx_status_t status) mutable {
      async::PostTask(dispatcher(), [this, status, cb = std::move(cb)]() mutable {
        // 3) Ensure the underlying disk has also flushed.
        {
          fs::WriteTxn sync_txn(this);
          sync_txn.EnqueueFlush();
          sync_txn.Transact();
          // Although the transaction shouldn't reference 'this'
          // after completing, scope it here to be extra cautious.
        }

        metrics_.Dump();
        flush_loop_.Shutdown();

        auto on_unmount = std::move(on_unmount_);

        // Manually destroy Blobfs. The promise of Shutdown is that no
        // connections are active, and destroying the Blobfs object
        // should terminate all background workers.
        delete this;

        // Identify to the unmounting channel that we've completed teardown.
        cb(status);

        // Identify to the mounting thread that the filesystem has
        // terminated.
        if (on_unmount) {
          on_unmount();
        }
      });
    });
  });
}

void Blobfs::WriteBitmap(uint64_t nblocks, uint64_t start_block,
                         fs::UnbufferedOperationsBuilder* operations) {
  TRACE_DURATION("blobfs", "Blobfs::WriteBitmap", "nblocks", nblocks, "start_block", start_block);
  uint64_t bbm_start_block = start_block / kBlobfsBlockBits;
  uint64_t bbm_end_block =
      fbl::round_up(start_block + nblocks, kBlobfsBlockBits) / kBlobfsBlockBits;

  // Write back the block allocation bitmap
  fs::UnbufferedOperation operation = {
      .vmo = zx::unowned_vmo(allocator_->GetBlockMapVmo().get()),
      {
          .type = fs::OperationType::kWrite,
          .vmo_offset = bbm_start_block,
          .dev_offset = BlockMapStartBlock(info_) + bbm_start_block,
          .length = bbm_end_block - bbm_start_block,
      }};
  operations->Add(std::move(operation));
}

void Blobfs::WriteNode(uint32_t map_index, fs::UnbufferedOperationsBuilder* operations) {
  TRACE_DURATION("blobfs", "Blobfs::WriteNode", "map_index", map_index);
  uint64_t block = (map_index * sizeof(Inode)) / kBlobfsBlockSize;
  fs::UnbufferedOperation operation = {.vmo = zx::unowned_vmo(allocator_->GetNodeMapVmo().get()),
                                       {
                                           .type = fs::OperationType::kWrite,
                                           .vmo_offset = block,
                                           .dev_offset = NodeMapStartBlock(info_) + block,
                                           .length = 1,
                                       }};
  operations->Add(std::move(operation));
}

void Blobfs::WriteInfo(fs::UnbufferedOperationsBuilder* operations) {
  memcpy(info_mapping_.start(), &info_, sizeof(info_));
  fs::UnbufferedOperation operation = {
      .vmo = zx::unowned_vmo(info_mapping_.vmo().get()),
      {
          .type = fs::OperationType::kWrite,
          .vmo_offset = 0,
          .dev_offset = 0,
          .length = 1,
      },
  };
  operations->Add(std::move(operation));
}

zx_status_t Blobfs::CreateFsId() {
  ZX_DEBUG_ASSERT(!fs_id_);
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

  fs_id_ = info.koid;
  return ZX_OK;
}

typedef struct dircookie {
  size_t index;       // Index into node map
  uint64_t reserved;  // Unused
} dircookie_t;

static_assert(sizeof(dircookie_t) <= sizeof(fs::vdircookie_t),
              "Blobfs dircookie too large to fit in IO state");

zx_status_t Blobfs::Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                            size_t* out_actual) {
  TRACE_DURATION("blobfs", "Blobfs::Readdir", "len", len);
  fs::DirentFiller df(dirents, len);
  dircookie_t* c = reinterpret_cast<dircookie_t*>(cookie);

  for (size_t i = c->index; i < info_.inode_count; ++i) {
    ZX_DEBUG_ASSERT(i < std::numeric_limits<uint32_t>::max());
    uint32_t node_index = static_cast<uint32_t>(i);
    if (GetNode(node_index)->header.IsAllocated() &&
        !GetNode(node_index)->header.IsExtentContainer()) {
      Digest digest(GetNode(node_index)->merkle_root_hash);
      char name[Digest::kLength * 2 + 1];
      zx_status_t r = digest.ToString(name, sizeof(name));
      if (r < 0) {
        return r;
      }
      uint64_t ino = fuchsia_io_INO_UNKNOWN;
      if ((r = df.Next(fbl::StringPiece(name, Digest::kLength * 2), VTYPE_TO_DTYPE(V_TYPE_FILE),
                       ino)) != ZX_OK) {
        break;
      }
      c->index = i + 1;
    }
  }

  *out_actual = df.BytesFilled();
  return ZX_OK;
}

zx_status_t Blobfs::RunOperation(const fs::Operation& operation, fs::BlockBuffer* buffer) {
  if (operation.type != fs::OperationType::kWrite && operation.type != fs::OperationType::kRead) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  block_fifo_request_t request;
  request.group = BlockGroupID();
  request.vmoid = buffer->vmoid();
  request.opcode = operation.type == fs::OperationType::kWrite ? BLOCKIO_WRITE : BLOCKIO_READ;
  request.vmo_offset = BlockNumberToDevice(operation.vmo_offset);
  request.dev_offset = BlockNumberToDevice(operation.dev_offset);
  uint64_t length = BlockNumberToDevice(operation.length);
  ZX_ASSERT_MSG(length < UINT32_MAX, "Request size too large");
  request.length = static_cast<uint32_t>(length);

  return block_device_->FifoTransaction(&request, 1);
}

groupid_t Blobfs::BlockGroupID() {
  thread_local groupid_t group_ = next_group_.fetch_add(1);
  ZX_ASSERT_MSG(group_ < MAX_TXN_GROUP_COUNT, "Too many threads accessing block device");
  return group_;
}

zx_status_t Blobfs::AttachVmo(const zx::vmo& vmo, vmoid_t* out) {
  fuchsia_hardware_block_VmoID vmoid;
  zx_status_t status = Device()->BlockAttachVmo(vmo, &vmoid);
  if (status != ZX_OK) {
    return status;
  }

  *out = vmoid.id;
  return ZX_OK;
}

zx_status_t Blobfs::DetachVmo(vmoid_t vmoid) {
  block_fifo_request_t request;
  request.group = BlockGroupID();
  request.vmoid = vmoid;
  request.opcode = BLOCKIO_CLOSE_VMO;
  return Transaction(&request, 1);
}

zx_status_t Blobfs::AddInodes(fzl::ResizeableVmoMapper* node_map) {
  TRACE_DURATION("blobfs", "Blobfs::AddInodes");

  if (!(info_.flags & kBlobFlagFVM)) {
    return ZX_ERR_NO_SPACE;
  }

  const size_t blocks_per_slice = info_.slice_size / kBlobfsBlockSize;
  uint64_t offset = (kFVMNodeMapStart / blocks_per_slice) + info_.ino_slices;
  uint64_t length = 1;
  zx_status_t status = Device()->VolumeExtend(offset, length);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Blobfs::AddInodes fvm_extend failure: %s", zx_status_get_string(status));
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

  if (node_map->Grow(inoblks * kBlobfsBlockSize) != ZX_OK) {
    return ZX_ERR_NO_SPACE;
  }

  info_.vslice_count += length;
  info_.ino_slices += static_cast<uint32_t>(length);
  info_.inode_count = inodes;

  // Reset new inodes to 0, and update the info block.
  uint64_t zeroed_nodes_blocks = inoblks - inoblks_old;
  uintptr_t addr = reinterpret_cast<uintptr_t>(node_map->start());
  memset(reinterpret_cast<void*>(addr + kBlobfsBlockSize * inoblks_old), 0,
         (kBlobfsBlockSize * (zeroed_nodes_blocks)));

  fs::UnbufferedOperationsBuilder builder;
  WriteInfo(&builder);
  if (zeroed_nodes_blocks > 0) {
    fs::UnbufferedOperation operation = {
        .vmo = zx::unowned_vmo(node_map->vmo().get()),
        {
            .type = fs::OperationType::kWrite,
            .vmo_offset = inoblks_old,
            .dev_offset = NodeMapStartBlock(info_) + inoblks_old,
            .length = zeroed_nodes_blocks,
        },
    };
    builder.Add(std::move(operation));
  }
  journal_->schedule_task(journal_->WriteMetadata(builder.TakeOperations()));
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
    FS_TRACE_ERROR("Blobfs::AddBlocks needs to increase block bitmap size\n");
    return ZX_ERR_NO_SPACE;
  }

  zx_status_t status = Device()->VolumeExtend(offset, length);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Blobfs::AddBlocks FVM Extend failure: %s\n", zx_status_get_string(status));
    return status;
  }

  // Grow the block bitmap to hold new number of blocks
  if (block_map->Grow(fbl::round_up(blocks, kBlobfsBlockBits)) != ZX_OK) {
    return ZX_ERR_NO_SPACE;
  }
  // Grow before shrinking to ensure the underlying storage is a multiple
  // of kBlobfsBlockSize.
  block_map->Shrink(blocks);

  info_.vslice_count += length;
  info_.dat_slices += static_cast<uint32_t>(length);
  info_.data_block_count = blocks;

  fs::UnbufferedOperationsBuilder builder;
  WriteInfo(&builder);
  uint64_t zeroed_bitmap_blocks = abmblks - abmblks_old;
  // Since we are extending the bitmap, we need to fill the expanded
  // portion of the allocation block bitmap with zeroes.
  if (zeroed_bitmap_blocks > 0) {
    fs::UnbufferedOperation operation = {
        .vmo = zx::unowned_vmo(block_map->StorageUnsafe()->GetVmo().get()),
        {
            .type = fs::OperationType::kWrite,
            .vmo_offset = abmblks_old,
            .dev_offset = BlockMapStartBlock(info_) + abmblks_old,
            .length = zeroed_bitmap_blocks,
        },
    };
    builder.Add(std::move(operation));
  }
  journal_->schedule_task(journal_->WriteMetadata(builder.TakeOperations()));
  return ZX_OK;
}

void Blobfs::Sync(SyncCallback closure) {
  if (journal_ == nullptr) {
    return closure(ZX_OK);
  }
  journal_->schedule_task(journal_->Sync().then(
      [closure = std::move(closure)](
          fit::result<void, zx_status_t>& result) mutable -> fit::result<void, zx_status_t> {
        if (result.is_ok()) {
          closure(ZX_OK);
        } else {
          closure(result.error());
        }
        return fit::ok();
      }));
}

Blobfs::Blobfs(std::unique_ptr<BlockDevice> device, const Superblock* info)
    : block_device_(std::move(device)), metrics_() {
  memcpy(&info_, info, sizeof(Superblock));
}

Blobfs::~Blobfs() {
  journal_.reset();
  Cache().Reset();
}

void Blobfs::ScheduleMetricFlush() {
  metrics_.mutable_collector()->Flush();
  async::PostDelayedTask(
      flush_loop_.dispatcher(), [this]() { ScheduleMetricFlush(); }, kCobaltFlushTimer);
}

zx_status_t Blobfs::Create(std::unique_ptr<BlockDevice> device, MountOptions* options,
                           std::unique_ptr<Blobfs>* out) {
  TRACE_DURATION("blobfs", "Blobfs::Create");
  char block[kBlobfsBlockSize];
  zx_status_t status = device->ReadBlock(0, kBlobfsBlockSize, block);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: could not read info block\n");
    return status;
  }
  const Superblock* superblock = reinterpret_cast<Superblock*>(&block[0]);

  fuchsia_hardware_block_BlockInfo block_info;
  status = device->BlockGetInfo(&block_info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: cannot acquire block info: %d\n", status);
    return status;
  }
  uint64_t blocks = (block_info.block_size * block_info.block_count) / kBlobfsBlockSize;
  if (block_info.flags & BLOCK_FLAG_READONLY) {
    FS_TRACE_WARN("blobfs: Mounting as read-only. WARNING: Journal will not be applied\n");
    options->writability = blobfs::Writability::ReadOnlyDisk;
  }
  if (kBlobfsBlockSize % block_info.block_size != 0) {
    FS_TRACE_ERROR("blobfs: Blobfs block size (%u) not divisible by device block size (%u)\n",
                   kBlobfsBlockSize, block_info.block_size);
    return ZX_ERR_IO;
  }

  // Construct the Blobfs object, without intensive validation, since it may require
  // upgrades / journal replays to become valid.
  auto fs = std::unique_ptr<Blobfs>(new Blobfs(std::move(device), superblock));
  fs->block_info_ = std::move(block_info);

  // Perform superblock validations which should succeed prior to journal replay.
  if (blocks < TotalBlocks(fs->Info())) {
    FS_TRACE_ERROR("blobfs: Block size mismatch: (superblock: %zu) vs (actual: %zu)\n",
                   TotalBlocks(fs->Info()), blocks);
    return ZX_ERR_BAD_STATE;
  }
  status = CheckSuperblock(&fs->Info(), TotalBlocks(fs->Info()));
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Check Superblock failure\n");
    return status;
  }

  if (options->metrics) {
    fs->Metrics().Collect();
    // TODO(gevalentino): Once we have async llcpp bindings, instead pass a dispatcher for
    // handling collector IPCs.
    fs->flush_loop_.StartThread("blobfs-metric-flusher");
    Blobfs* fsptr = fs.get();
    async::PostDelayedTask(
        fs->flush_loop_.dispatcher(), [fsptr]() { fsptr->ScheduleMetricFlush(); },
        kCobaltFlushTimer);
  }

  if (options->journal) {
    if (options->writability == blobfs::Writability::ReadOnlyDisk) {
      FS_TRACE_ERROR("blobfs: Replaying the journal requires a writable disk\n");
      return ZX_ERR_ACCESS_DENIED;
    }
    FS_TRACE_INFO("blobfs: Replaying journal\n");
    JournalSuperblock journal_superblock;
    status = fs::ReplayJournal(fs.get(), fs.get(), JournalStartBlock(fs->info_),
                               JournalBlocks(fs->info_), &journal_superblock);
    if (status != ZX_OK) {
      FS_TRACE_ERROR("blobfs: Failed to replay journal\n");
      return status;
    }
    FS_TRACE_DEBUG("blobfs: Journal replayed\n");

    switch (options->writability) {
      case blobfs::Writability::Writable:
        FS_TRACE_DEBUG("blobfs: Initializing journal for writeback\n");
        status = InitializeJournal(fs.get(), fs.get(), JournalStartBlock(fs->info_),
                                   JournalBlocks(fs->info_), std::move(journal_superblock),
                                   &fs->journal_);
        if (status != ZX_OK) {
          FS_TRACE_ERROR("blobfs: Failed to initialize journal\n");
          return status;
        }
        status = fs->Reload();
        if (status != ZX_OK) {
          FS_TRACE_ERROR("blobfs: Failed to re-load superblock\n");
          return status;
        }
        break;
      case blobfs::Writability::ReadOnlyFilesystem:
        // Journal uninitialized.
        break;
      default:
        FS_TRACE_ERROR("blobfs: Unexpected writability option for journaling\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
  } else if (options->writability == blobfs::Writability::Writable) {
    FS_TRACE_INFO("blobfs: Initializing writeback (no journal)\n");
    status = InitializeUnjournalledWriteback(fs.get(), fs.get(), &fs->journal_);
    if (status != ZX_OK) {
      FS_TRACE_ERROR("blobfs: Failed to initialize writeback (unjournaled)\n");
      return status;
    }
  }

  fs->SetReadonly(options->writability != blobfs::Writability::Writable);
  fs->Cache().SetCachePolicy(options->cache_policy);
  RawBitmap block_map;
  // Keep the block_map aligned to a block multiple
  if ((status = block_map.Reset(BlockMapBlocks(fs->info_) * kBlobfsBlockBits)) < 0) {
    FS_TRACE_ERROR("blobfs: Could not reset block bitmap\n");
    return status;
  } else if ((status = block_map.Shrink(fs->info_.data_block_count)) < 0) {
    FS_TRACE_ERROR("blobfs: Could not shrink block bitmap\n");
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
    FS_TRACE_ERROR("blobfs: Failed to allocate bitmap for inodes\n");
    return status;
  }

  fs->allocator_ = std::make_unique<Allocator>(fs.get(), std::move(block_map), std::move(node_map),
                                               std::move(nodes_bitmap));
  if ((status = fs->allocator_->ResetFromStorage(fs::ReadTxn(fs.get()))) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to load bitmaps: %d\n", status);
    return status;
  }

  if ((status = fs->info_mapping_.CreateAndMap(kBlobfsBlockSize, "blobfs-superblock")) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to create info vmo: %d\n", status);
    return status;
  } else if ((status = fs->AttachVmo(fs->info_mapping_.vmo(), &fs->info_vmoid_)) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to attach info vmo: %d\n", status);
    return status;
  } else if ((status = fs->CreateFsId()) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to create fs_id: %d\n", status);
    return status;
  } else if ((status = fs->InitializeVnodes()) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to initialize Vnodes\n");
    return status;
  }

  *out = std::move(fs);
  return ZX_OK;
}

zx_status_t Blobfs::InitializeVnodes() {
  Cache().Reset();
  uint32_t total_allocated = 0;

  for (uint32_t node_index = 0; node_index < info_.inode_count; node_index++) {
    const Inode* inode = GetNode(node_index);
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
    Digest digest(inode->merkle_root_hash);
    fbl::RefPtr<Blob> vnode = fbl::AdoptRef(new Blob(this, digest));
    vnode->SetState(kBlobStateReadable);
    vnode->PopulateInode(node_index);

    // This blob is added to the cache, where it will quickly be relocated into the "closed
    // set" once we drop our reference to |vnode|. Although we delay reading any of the
    // contents of the blob from disk until requested, this pre-caching scheme allows us to
    // quickly verify or deny the presence of a blob during blob lookup and creation.
    zx_status_t status = Cache().Add(vnode);
    if (status != ZX_OK) {
      Digest digest(vnode->GetNode().merkle_root_hash);
      char name[digest::Digest::kLength * 2 + 1];
      digest.ToString(name, sizeof(name));
      FS_TRACE_ERROR("blobfs: CORRUPTED FILESYSTEM: Duplicate node: %s @ index %u\n", name,
                     node_index - 1);
      return status;
    }
    Metrics().UpdateLookup(vnode->SizeData());
  }

  if (total_allocated != info_.alloc_inode_count) {
    FS_TRACE_ERROR(
        "blobfs: CORRUPTED FILESYSTEM: Allocated nodes mismatch. Expected:%lu. Found: %u\n",
        info_.alloc_inode_count, total_allocated);
    return ZX_ERR_IO_OVERRUN;
  }

  return ZX_OK;
}

zx_status_t Blobfs::Reload() {
  TRACE_DURATION("blobfs", "Blobfs::Reload");

  // Re-read the info block from disk.
  char block[kBlobfsBlockSize];
  zx_status_t status = Device()->ReadBlock(0, kBlobfsBlockSize, block);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: could not read info block\n");
    return status;
  }

  Superblock* info = reinterpret_cast<Superblock*>(&block[0]);
  if ((status = CheckSuperblock(info, TotalBlocks(*info))) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Check info failure\n");
    return status;
  }

  // Once it has been verified, overwrite the current info.
  memcpy(&info_, info, sizeof(Superblock));
  return ZX_OK;
}

zx_status_t Blobfs::OpenRootNode(fbl::RefPtr<Directory>* out) {
  fbl::RefPtr<Directory> vn = fbl::AdoptRef(new Directory(this));

  zx_status_t status = vn->Open(0, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  *out = std::move(vn);
  return ZX_OK;
}

Journal* Blobfs::journal() { return journal_.get(); }

}  // namespace blobfs
