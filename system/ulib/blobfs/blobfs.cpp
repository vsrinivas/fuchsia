// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <limits>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <blobfs/blobfs.h>
#include <blobfs/extent-reserver.h>
#include <blobfs/lz4.h>
#include <blobfs/node-reserver.h>
#include <blobfs/writeback.h>
#include <cobalt-client/cpp/collector.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>
#include <fs/block-txn.h>
#include <fs/ticker.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/debug.h>
#include <lib/zx/event.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#define ZXDEBUG 0

#include <utility>

using digest::Digest;
using digest::MerkleTree;

namespace blobfs {
namespace {

cobalt_client::CollectorOptions MakeCollectorOptions() {
    cobalt_client::CollectorOptions options = cobalt_client::CollectorOptions::Debug();
#ifdef __Fuchsia__
    // Reads from boot the cobalt_filesystem.pb
    options.load_config = [](zx::vmo* out_vmo, size_t* out_size) -> bool {
        fbl::unique_fd config_fd(open("/boot/config/cobalt_filesystem.pb", O_RDONLY));
        if (!config_fd) {
            return false;
        }
        *out_size = lseek(config_fd.get(), 0L, SEEK_END);
        if (*out_size <= 0) {
            return false;
        }
        zx_status_t result = zx::vmo::create(*out_size, 0, out_vmo);
        if (result != ZX_OK) {
            return false;
        }
        fbl::Array<uint8_t> buffer(new uint8_t[*out_size], *out_size);
        memset(buffer.get(), 0, *out_size);
        if (lseek(config_fd.get(), 0L, SEEK_SET) < 0) {
            return false;
        }
        if (static_cast<uint32_t>(read(config_fd.get(), buffer.get(), buffer.size())) < *out_size) {
            return false;
        }
        return out_vmo->write(buffer.get(), 0, *out_size) == ZX_OK;
    };
    options.initial_response_deadline = zx::usec(0);
    options.response_deadline = zx::nsec(0);
#endif // __Fuchsia__
    return options;
}

zx_status_t CheckFvmConsistency(const Superblock* info, int block_fd) {
    if ((info->flags & kBlobFlagFVM) == 0) {
        return ZX_OK;
    }

    fvm_info_t fvm_info;
    zx_status_t status = static_cast<zx_status_t>(ioctl_block_fvm_query(block_fd, &fvm_info));
    if (status < ZX_OK) {
        FS_TRACE_ERROR("blobfs: Unable to query FVM, fd: %d status: 0x%x\n", block_fd, status);
        return ZX_ERR_UNAVAILABLE;
    }

    if (info->slice_size != fvm_info.slice_size) {
        FS_TRACE_ERROR("blobfs: Slice size did not match expected\n");
        return ZX_ERR_BAD_STATE;
    }
    const size_t kBlocksPerSlice = info->slice_size / kBlobfsBlockSize;

    size_t expected_count[4];
    expected_count[0] = info->abm_slices;
    expected_count[1] = info->ino_slices;
    expected_count[2] = info->journal_slices;
    expected_count[3] = info->dat_slices;

    query_request_t request;
    request.count = 4;
    request.vslice_start[0] = kFVMBlockMapStart / kBlocksPerSlice;
    request.vslice_start[1] = kFVMNodeMapStart / kBlocksPerSlice;
    request.vslice_start[2] = kFVMJournalStart / kBlocksPerSlice;
    request.vslice_start[3] = kFVMDataStart / kBlocksPerSlice;

    query_response_t response;
    status = static_cast<zx_status_t>(ioctl_block_fvm_vslice_query(block_fd, &request, &response));
    if (status < ZX_OK) {
        FS_TRACE_ERROR("blobfs: Unable to query slices, status: 0x%x\n", status);
        return ZX_ERR_UNAVAILABLE;
    }

    if (response.count != request.count) {
        FS_TRACE_ERROR("blobfs: Missing slice\n");
        return ZX_ERR_BAD_STATE;
    }

    for (size_t i = 0; i < request.count; i++) {
        size_t blobfs_count = expected_count[i];
        size_t fvm_count = response.vslice_range[i].count;

        if (!response.vslice_range[i].allocated || fvm_count < blobfs_count) {
            // Currently, since Blobfs can only grow new slices, it should not be possible for
            // the FVM to report a slice size smaller than what is reported by Blobfs. In this
            // case, automatically fail without trying to resolve the situation, as it is
            // possible that Blobfs structures are allocated in the slices that have been lost.
            FS_TRACE_ERROR("blobfs: Mismatched slice count\n");
            return ZX_ERR_IO_DATA_INTEGRITY;
        }

        if (fvm_count > blobfs_count) {
            // If FVM reports more slices than we expect, try to free remainder.
            extend_request_t shrink;
            shrink.length = fvm_count - blobfs_count;
            shrink.offset = request.vslice_start[i] + blobfs_count;
            ssize_t r;
            if ((r = ioctl_block_fvm_shrink(block_fd, &shrink)) != ZX_OK) {
                FS_TRACE_ERROR("blobfs: Unable to shrink to expected size, status: %zd\n", r);
                return ZX_ERR_IO_DATA_INTEGRITY;
            }
        }
    }

    return ZX_OK;
}

} // namespace

zx_status_t Blobfs::VerifyBlob(uint32_t node_index) {
    return Blob::VerifyBlob(this, node_index);
}

void Blobfs::PersistBlocks(WritebackWork* wb, const ReservedExtent& reserved_extent) {
    TRACE_DURATION("blobfs", "Blobfs::PersistBlocks");

    allocator_->MarkBlocksAllocated(reserved_extent);

    const Extent& extent = reserved_extent.extent();
    info_.alloc_block_count += extent.Length();
    // Write out to disk.
    WriteBitmap(wb, extent.Length(), extent.Start());
    WriteInfo(wb);
}

// Frees blocks from reserved and allocated maps, updates disk in the latter case.
void Blobfs::FreeExtent(WritebackWork* wb, const Extent& extent) {
    size_t start = extent.Start();
    size_t num_blocks = extent.Length();
    size_t end = start + num_blocks;

    TRACE_DURATION("blobfs", "Blobfs::FreeExtent", "nblocks", num_blocks, "blkno", start);

    // Check if blocks were allocated on disk.
    if (allocator_->CheckBlocksAllocated(start, end)) {
        allocator_->FreeBlocks(extent);
        info_.alloc_block_count -= num_blocks;
        WriteBitmap(wb, num_blocks, start);
        WriteInfo(wb);
    }
}

void Blobfs::FreeNode(WritebackWork* wb, uint32_t node_index) {
    allocator_->FreeNode(node_index);
    info_.alloc_inode_count--;
    WriteNode(wb, node_index);
}

void Blobfs::FreeInode(WritebackWork* wb, uint32_t node_index) {
    TRACE_DURATION("blobfs", "Blobfs::FreeInode", "node_index", node_index);
    Inode* mapped_inode = GetNode(node_index);
    ZX_DEBUG_ASSERT(wb != nullptr);

    if (mapped_inode->header.IsAllocated()) {
        // Always write back the first node.
        FreeNode(wb, node_index);

        AllocatedExtentIterator extent_iter(allocator_.get(), node_index);
        while (!extent_iter.Done()) {
            // If we're observing a new node, free it.
            if (extent_iter.NodeIndex() != node_index) {
                node_index = extent_iter.NodeIndex();
                FreeNode(wb, node_index);
            }

            const Extent* extent;
            ZX_ASSERT(extent_iter.Next(&extent) == ZX_OK);

            // Free the extent.
            FreeExtent(wb, *extent);
        }
        WriteInfo(wb);
    }
}

void Blobfs::PersistNode(WritebackWork* wb, uint32_t node_index) {
    TRACE_DURATION("blobfs", "Blobfs::PersistNode");
    info_.alloc_inode_count++;
    WriteNode(wb, node_index);
    WriteInfo(wb);
}

zx_status_t Blobfs::InitializeWriteback(const MountOptions& options) {
    if (options.readonly) {
        // If blobfs should be readonly, do not start up any writeback threads.
        return ZX_OK;
    }

    // Initialize the WritebackQueue.
    zx_status_t status =
        WritebackQueue::Create(this, WriteBufferSize() / kBlobfsBlockSize, &writeback_);

    if (status != ZX_OK) {
        return status;
    }

    // Replay any lingering journal entries.
    if ((status = journal_->Replay()) != ZX_OK) {
        return status;
    }

    // TODO(ZX-2728): Don't load metadata until after journal replay.
    // Re-load blobfs metadata from disk, since things may have changed.
    if ((status = Reload()) != ZX_OK) {
        return status;
    }

    if (options.journal) {
        // Initialize the journal's writeback thread (if journaling is enabled).
        // Wait until after replay has completed in order to avoid concurrency issues.
        return journal_->InitWriteback();
    }

    // If journaling is disabled, delete the journal.
    journal_.reset();
    return ZX_OK;
}

size_t Blobfs::WritebackCapacity() const {
    return writeback_->GetCapacity();
}

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
            async::PostTask(dispatcher(), [this, cb = std::move(cb)]() mutable {
                // 3) Ensure the underlying disk has also flushed.
                {
                    fs::WriteTxn sync_txn(this);
                    sync_txn.EnqueueFlush();
                    sync_txn.Transact();
                    // Although the transaction shouldn't reference 'this'
                    // after completing, scope it here to be extra cautious.
                }

                metrics_.Dump();

                auto on_unmount = std::move(on_unmount_);

                // Explicitly tear down the journal and writeback threads in case any unexpected
                // errors occur.
                zx_status_t journal_status = ZX_OK, writeback_status = ZX_OK;
                if (journal_ != nullptr) {
                    journal_status = journal_->Teardown();
                }

                if (writeback_ != nullptr) {
                    writeback_status = writeback_->Teardown();
                }

                // Manually destroy Blobfs. The promise of Shutdown is that no
                // connections are active, and destroying the Blobfs object
                // should terminate all background workers.
                delete this;

                // Identify to the unmounting channel that we've completed teardown.
                if (journal_status != ZX_OK) {
                    cb(journal_status);
                } else {
                    cb(writeback_status);
                }

                // Identify to the mounting thread that the filesystem has
                // terminated.
                if (on_unmount) {
                    on_unmount();
                }
            });
        });
    });
}

void Blobfs::WriteBitmap(WritebackWork* wb, uint64_t nblocks, uint64_t start_block) {
    TRACE_DURATION("blobfs", "Blobfs::WriteBitmap", "nblocks", nblocks, "start_block", start_block);
    uint64_t bbm_start_block = start_block / kBlobfsBlockBits;
    uint64_t bbm_end_block =
        fbl::round_up(start_block + nblocks, kBlobfsBlockBits) / kBlobfsBlockBits;

    // Write back the block allocation bitmap
    wb->Enqueue(allocator_->GetBlockMapVmo(), bbm_start_block,
                BlockMapStartBlock(info_) + bbm_start_block, bbm_end_block - bbm_start_block);
}

void Blobfs::WriteNode(WritebackWork* wb, uint32_t map_index) {
    TRACE_DURATION("blobfs", "Blobfs::WriteNode", "map_index", map_index);
    uint64_t b = (map_index * sizeof(Inode)) / kBlobfsBlockSize;
    wb->Enqueue(allocator_->GetNodeMapVmo(), b, NodeMapStartBlock(info_) + b, 1);
}

void Blobfs::WriteInfo(WritebackWork* wb) {
    memcpy(info_mapping_.start(), &info_, sizeof(info_));
    wb->Enqueue(info_mapping_.vmo(), 0, 0, 1);
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
    size_t index;      // Index into node map
    uint64_t reserved; // Unused
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
            if ((r = df.Next(fbl::StringPiece(name, Digest::kLength * 2),
                             VTYPE_TO_DTYPE(V_TYPE_FILE), ino)) != ZX_OK) {
                break;
            }
            c->index = i + 1;
        }
    }

    *out_actual = df.BytesFilled();
    return ZX_OK;
}

zx_status_t Blobfs::AttachVmo(const zx::vmo& vmo, vmoid_t* out) {
    zx::vmo xfer_vmo;
    zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &xfer_vmo);
    if (status != ZX_OK) {
        return status;
    }
    zx_handle_t raw_vmo = xfer_vmo.release();
    ssize_t r = ioctl_block_attach_vmo(Fd(), &raw_vmo, out);
    if (r < 0) {
        return static_cast<zx_status_t>(r);
    }
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

    const size_t kBlocksPerSlice = info_.slice_size / kBlobfsBlockSize;
    extend_request_t request;
    request.length = 1;
    request.offset = (kFVMNodeMapStart / kBlocksPerSlice) + info_.ino_slices;
    if (ioctl_block_fvm_extend(Fd(), &request) < 0) {
        FS_TRACE_ERROR("Blobfs::AddInodes fvm_extend failure");
        return ZX_ERR_NO_SPACE;
    }

    const uint32_t kInodesPerSlice = static_cast<uint32_t>(info_.slice_size / kBlobfsInodeSize);
    uint64_t inodes64 =
        (info_.ino_slices + static_cast<uint32_t>(request.length)) * kInodesPerSlice;
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

    info_.vslice_count += request.length;
    info_.ino_slices += static_cast<uint32_t>(request.length);
    info_.inode_count = inodes;

    // Reset new inodes to 0
    uintptr_t addr = reinterpret_cast<uintptr_t>(node_map->start());
    memset(reinterpret_cast<void*>(addr + kBlobfsBlockSize * inoblks_old), 0,
           (kBlobfsBlockSize * (inoblks - inoblks_old)));

    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = CreateWork(&wb, nullptr)) != ZX_OK) {
        return status;
    }

    WriteInfo(wb.get());
    wb.get()->Enqueue(node_map->vmo(), inoblks_old, NodeMapStartBlock(info_) + inoblks_old,
                      inoblks - inoblks_old);
    return EnqueueWork(std::move(wb), EnqueueType::kJournal);
}

zx_status_t Blobfs::AddBlocks(size_t nblocks, RawBitmap* block_map) {
    TRACE_DURATION("blobfs", "Blobfs::AddBlocks", "nblocks", nblocks);

    if (!(info_.flags & kBlobFlagFVM)) {
        return ZX_ERR_NO_SPACE;
    }

    const size_t kBlocksPerSlice = info_.slice_size / kBlobfsBlockSize;
    extend_request_t request;
    // Number of slices required to add nblocks
    request.length = (nblocks + kBlocksPerSlice - 1) / kBlocksPerSlice;
    request.offset = (kFVMDataStart / kBlocksPerSlice) + info_.dat_slices;

    uint64_t blocks64 = (info_.dat_slices + request.length) * kBlocksPerSlice;
    ZX_DEBUG_ASSERT(blocks64 <= std::numeric_limits<uint32_t>::max());
    uint32_t blocks = static_cast<uint32_t>(blocks64);
    uint32_t abmblks = (blocks + kBlobfsBlockBits - 1) / kBlobfsBlockBits;
    uint64_t abmblks_old = (info_.data_block_count + kBlobfsBlockBits - 1) / kBlobfsBlockBits;
    ZX_DEBUG_ASSERT(abmblks_old <= abmblks);

    if (abmblks > kBlocksPerSlice) {
        // TODO(planders): Allocate more slices for the block bitmap.
        FS_TRACE_ERROR("Blobfs::AddBlocks needs to increase block bitmap size\n");
        return ZX_ERR_NO_SPACE;
    }

    if (ioctl_block_fvm_extend(Fd(), &request) < 0) {
        FS_TRACE_ERROR("Blobfs::AddBlocks FVM Extend failure\n");
        return ZX_ERR_NO_SPACE;
    }

    // Grow the block bitmap to hold new number of blocks
    if (block_map->Grow(fbl::round_up(blocks, kBlobfsBlockBits)) != ZX_OK) {
        return ZX_ERR_NO_SPACE;
    }
    // Grow before shrinking to ensure the underlying storage is a multiple
    // of kBlobfsBlockSize.
    block_map->Shrink(blocks);

    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = CreateWork(&wb, nullptr)) != ZX_OK) {
        return status;
    }

    // Since we are extending the bitmap, we need to fill the expanded
    // portion of the allocation block bitmap with zeroes.
    if (abmblks > abmblks_old) {
        uint64_t vmo_offset = abmblks_old;
        uint64_t dev_offset = BlockMapStartBlock(info_) + abmblks_old;
        uint64_t length = abmblks - abmblks_old;
        wb.get()->Enqueue(block_map->StorageUnsafe()->GetVmo(), vmo_offset, dev_offset, length);
    }

    info_.vslice_count += request.length;
    info_.dat_slices += static_cast<uint32_t>(request.length);
    info_.data_block_count = blocks;

    WriteInfo(wb.get());
    return EnqueueWork(std::move(wb), EnqueueType::kJournal);
}

void Blobfs::Sync(SyncCallback closure) {
    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = CreateWork(&wb, nullptr)) != ZX_OK) {
        closure(status);
        return;
    }

    wb->SetSyncCallback(std::move(closure));
    // This may return an error, but it doesn't matter - the closure will be called anyway.
    status = EnqueueWork(std::move(wb), EnqueueType::kJournal);
}

Blobfs::Blobfs(fbl::unique_fd fd, const Superblock* info)
    : blockfd_(std::move(fd)), metrics_(),
      cobalt_metrics_(MakeCollectorOptions(), false, "blobfs") {
    memcpy(&info_, info, sizeof(Superblock));
}

Blobfs::~Blobfs() {
    // The journal must be destroyed before the writeback buffer, since it may still need
    // to enqueue more transactions for writeback.
    journal_.reset();
    writeback_.reset();

    Cache().Reset();

    if (blockfd_) {
        ioctl_block_fifo_close(Fd());
    }
}

zx_status_t Blobfs::Create(fbl::unique_fd fd, const MountOptions& options, const Superblock* info,
                           fbl::unique_ptr<Blobfs>* out) {
    TRACE_DURATION("blobfs", "Blobfs::Create");
    zx_status_t status = CheckSuperblock(info, TotalBlocks(*info));
    if (status < 0) {
        FS_TRACE_ERROR("blobfs: Check info failure\n");
        return status;
    }

    auto fs = fbl::unique_ptr<Blobfs>(new Blobfs(std::move(fd), info));
    fs->SetReadonly(options.readonly);
    fs->Cache().SetCachePolicy(options.cache_policy);
    if (options.metrics) {
        fs->LocalMetrics().Collect();
    }

    zx::fifo fifo;
    ssize_t r;
    if ((r = ioctl_block_get_info(fs->Fd(), &fs->block_info_)) < 0) {
        return static_cast<zx_status_t>(r);
    } else if (kBlobfsBlockSize % fs->block_info_.block_size != 0) {
        return ZX_ERR_IO;
    } else if ((r = ioctl_block_get_fifos(fs->Fd(), fifo.reset_and_get_address())) < 0) {
        FS_TRACE_ERROR("Failed to mount blobfs: Someone else is using the block device\n");
        return static_cast<zx_status_t>(r);
    }

    if ((status = block_client::Client::Create(std::move(fifo), &fs->fifo_client_)) != ZX_OK) {
        return status;
    }

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
    fs->allocator_ =
        fbl::make_unique<Allocator>(fs.get(), std::move(block_map), std::move(node_map));
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
    } else if ((status = fs->InitializeVnodes() != ZX_OK)) {
        FS_TRACE_ERROR("blobfs: Failed to initialize Vnodes\n");
        return status;
    }

    status = Journal::Create(fs.get(), JournalBlocks(fs->info_), JournalStartBlock(fs->info_),
                             &fs->journal_);
    if (status != ZX_OK) {
        return status;
    }

    *out = std::move(fs);
    return ZX_OK;
}

zx_status_t Blobfs::InitializeVnodes() {
    Cache().Reset();

    for (uint32_t node_index = 0; node_index < info_.inode_count; node_index++) {
        const Inode* inode = GetNode(node_index);
        if (inode->header.IsAllocated() && !inode->header.IsExtentContainer()) {
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
                FS_TRACE_ERROR("blobfs: CORRUPTED FILESYSTEM: Duplicate node: %s @ index %u\n",
                               name, node_index - 1);
                return status;
            }
            LocalMetrics().UpdateLookup(vnode->SizeData());
        }
    }

    return ZX_OK;
}

zx_status_t Blobfs::Reload() {
    TRACE_DURATION("blobfs", "Blobfs::Reload");

    // Re-read the info block from disk.
    zx_status_t status;
    char block[kBlobfsBlockSize];
    if ((status = readblk(Fd(), 0, block)) != ZX_OK) {
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

    // Ensure the block and node maps are up-to-date with changes in size that
    // might have happened.
    status = allocator_->ResetBlockMapSize();
    if (status != ZX_OK) {
        return status;
    }
    status = allocator_->ResetNodeMapSize();
    if (status != ZX_OK) {
        return status;
    }

    // Load the bitmaps from disk.
    if ((status = allocator_->ResetFromStorage(fs::ReadTxn(this))) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: Failed to load bitmaps: %d\n", status);
        return status;
    }

    // Load the vnodes from disk.
    if ((status = InitializeVnodes() != ZX_OK)) {
        FS_TRACE_ERROR("blobfs: Failed to initialize Vnodes\n");
        return status;
    }

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

zx_status_t Initialize(fbl::unique_fd blockfd, const MountOptions& options,
                       fbl::unique_ptr<Blobfs>* out) {
    zx_status_t status;

    char block[kBlobfsBlockSize];
    if ((status = readblk(blockfd.get(), 0, (void*)block)) < 0) {
        FS_TRACE_ERROR("blobfs: could not read info block\n");
        return status;
    }

    Superblock* info = reinterpret_cast<Superblock*>(&block[0]);

    uint64_t blocks;
    if ((status = GetBlockCount(blockfd.get(), &blocks)) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: cannot find end of underlying device\n");
        return status;
    }

    if ((status = CheckSuperblock(info, blocks)) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: Info check failed\n");
        return status;
    }

    if ((status = Blobfs::Create(std::move(blockfd), options, info, out)) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: mount failed; could not create blobfs\n");
        return status;
    }
    return ZX_OK;
}

zx_status_t Mount(async_dispatcher_t* dispatcher, fbl::unique_fd blockfd,
                  const MountOptions& options, zx::channel root, fbl::Closure on_unmount) {
    zx_status_t status;
    fbl::unique_ptr<Blobfs> fs;

    if ((status = Initialize(std::move(blockfd), options, &fs)) != ZX_OK) {
        return status;
    }

    // Attempt to initialize writeback and journal.
    // The journal must be replayed before the FVM check, in case changes to slice counts have
    // been written to the journal but not persisted to the super block.
    if ((status = fs->InitializeWriteback(options)) != ZX_OK) {
        return status;
    }

    if ((status = CheckFvmConsistency(&fs->Info(), fs->Fd())) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: FVM info check failed\n");
        return status;
    }

    fs->SetDispatcher(dispatcher);
    fs->SetUnmountCallback(std::move(on_unmount));

    fbl::RefPtr<Directory> vn;
    if ((status = fs->OpenRootNode(&vn)) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: mount failed; could not get root blob\n");
        return status;
    }

    if ((status = fs->ServeDirectory(std::move(vn), std::move(root))) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: mount failed; could not serve root directory\n");
        return status;
    }

    // Shutdown is now responsible for deleting the Blobfs object.
    __UNUSED auto r = fs.release();
    return ZX_OK;
}

zx_status_t Blobfs::CreateWork(fbl::unique_ptr<WritebackWork>* out, Blob* vnode) {
    if (writeback_ == nullptr) {
        // Transactions should never be allowed if the writeback queue is disabled.
        return ZX_ERR_BAD_STATE;
    }

    out->reset(new BlobWork(this, fbl::WrapRefPtr(vnode)));
    return ZX_OK;
}

zx_status_t Blobfs::EnqueueWork(fbl::unique_ptr<WritebackWork> work, EnqueueType type) {
    ZX_DEBUG_ASSERT(work != nullptr);
    switch (type) {
    case EnqueueType::kJournal:
        if (journal_ != nullptr) {
            // If journaling is enabled (both in general and for this WritebackWork),
            // attempt to enqueue to the journal buffer.
            return journal_->Enqueue(std::move(work));
        }
        // Even if our enqueue type is kJournal,
        // fall through to the writeback queue if the journal doesn't exist.
        __FALLTHROUGH;
    case EnqueueType::kData:
        if (writeback_ != nullptr) {
            return writeback_->Enqueue(std::move(work));
        }
        // If writeback_ does not exist, we are in a readonly state.
        // Fall through to the default case.
        __FALLTHROUGH;
    default:
        // The file system is currently in a readonly state.
        // Mark the work complete to ensure that any pending callbacks are invoked.
        work->MarkCompleted(ZX_ERR_BAD_STATE);
        return ZX_ERR_BAD_STATE;
    }
}
} // namespace blobfs
