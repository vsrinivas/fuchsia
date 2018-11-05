// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>
#include <fbl/ref_ptr.h>
#include <lib/fdio/debug.h>
#include <fs/block-txn.h>
#include <fs/ticker.h>
#include <lib/zx/event.h>
#include <lib/async/cpp/task.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#define ZXDEBUG 0

#include <blobfs/blobfs.h>
#include <blobfs/lz4.h>

using digest::Digest;
using digest::MerkleTree;

namespace blobfs {
namespace {

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

// A wrapper around "Enqueue" for content which risks being larger
// than the writeback buffer.
//
// For content which is smaller than 3/4 the size of the writeback buffer: the
// content is enqueued to |work| without flushing.
//
// For content which is larger than 3/4 the size of the writeback buffer: flush
// the data by enqueueing it to the writeback thread in chunks until the
// remainder is small enough to comfortably fit within the writeback buffer.
zx_status_t EnqueuePaginated(fbl::unique_ptr<WritebackWork>* work, Blobfs* blobfs, VnodeBlob* vn,
                             zx_handle_t vmo, uint64_t relative_block, uint64_t absolute_block,
                             uint64_t nblocks) {
    const size_t kMaxChunkBlocks = (3 * blobfs->WritebackCapacity()) / 4;
    uint64_t delta_blocks = fbl::min(nblocks, kMaxChunkBlocks);
    while (nblocks > 0) {
        (*work)->Enqueue(vmo, relative_block, absolute_block, delta_blocks);
        relative_block += delta_blocks;
        absolute_block += delta_blocks;
        nblocks -= delta_blocks;
        delta_blocks = fbl::min(nblocks, kMaxChunkBlocks);
        if (nblocks) {
            fbl::unique_ptr<WritebackWork> tmp;
            zx_status_t status = blobfs->CreateWork(&tmp, vn);
            if (status != ZX_OK) {
                return status;
            }
            if ((status = blobfs->EnqueueWork(fbl::move(*work), EnqueueType::kData)) != ZX_OK) {
                return status;
            }
            *work = fbl::move(tmp);
        }
    }
    return ZX_OK;
}

}  // namespace

Inode* Blobfs::GetNode(size_t index) const {
    return &reinterpret_cast<Inode*>(node_map_.start())[index];
}

zx_status_t VnodeBlob::Verify() const {
    TRACE_DURATION("blobfs", "Blobfs::Verify");
    fs::Ticker ticker(blobfs_->CollectingMetrics());

    const void* data = inode_.blob_size ? GetData() : nullptr;
    const void* tree = inode_.blob_size ? GetMerkle() : nullptr;
    const uint64_t data_size = inode_.blob_size;
    const uint64_t merkle_size = MerkleTree::GetTreeLength(data_size);
    // TODO(smklein): We could lazily verify more of the VMO if
    // we could fault in pages on-demand.
    //
    // For now, we aggressively verify the entire VMO up front.
    Digest digest;
    digest = reinterpret_cast<const uint8_t*>(&digest_[0]);
    zx_status_t status = MerkleTree::Verify(data, data_size, tree,
                                            merkle_size, 0, data_size, digest);
    blobfs_->UpdateMerkleVerifyMetrics(data_size, merkle_size, ticker.End());

    if (status != ZX_OK) {
        char name[Digest::kLength * 2 + 1];
        ZX_ASSERT(digest.ToString(name, sizeof(name)) == ZX_OK);
        FS_TRACE_ERROR("blobfs verify(%s) Failure: %s\n", name, zx_status_get_string(status));
    }

    return status;
}

zx_status_t VnodeBlob::InitVmos() {
    TRACE_DURATION("blobfs", "Blobfs::InitVmos");

    if (mapping_.vmo()) {
        return ZX_OK;
    }

    uint64_t data_blocks = BlobDataBlocks(inode_);
    uint64_t merkle_blocks = MerkleTreeBlocks(inode_);
    uint64_t num_blocks = data_blocks + merkle_blocks;

    if (num_blocks == 0) {
        // No need to initialize VMO for null blob.
        return ZX_OK;
    }

    // Reverts blob back to uninitialized state on error.
    auto cleanup = fbl::MakeAutoCall([this]() { BlobCloseHandles(); });

    size_t vmo_size;
    if (mul_overflow(num_blocks, kBlobfsBlockSize, &vmo_size)) {
        FS_TRACE_ERROR("Multiplication overflow");
        return ZX_ERR_OUT_OF_RANGE;
    }

    zx_status_t status = mapping_.CreateAndMap(vmo_size, "blob");
    if (status != ZX_OK) {
        FS_TRACE_ERROR("Failed to initialize vmo; error: %d\n", status);
        return status;
    }
    if ((status = blobfs_->AttachVmo(mapping_.vmo().get(), &vmoid_)) != ZX_OK) {
        FS_TRACE_ERROR("Failed to attach VMO to block device; error: %d\n", status);
        return status;
    }

    if ((inode_.flags & kBlobFlagLZ4Compressed) != 0) {
        if ((status = InitCompressed()) != ZX_OK) {
            return status;
        }
    } else {
        if ((status = InitUncompressed()) != ZX_OK) {
            return status;
        }
    }
    if ((status = Verify()) != ZX_OK) {
        return status;
    }

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t VnodeBlob::InitCompressed() {
    TRACE_DURATION("blobfs", "Blobfs::InitCompressed", "size", inode_.blob_size,
                   "blocks", inode_.num_blocks);
    fs::Ticker ticker(blobfs_->CollectingMetrics());
    fs::ReadTxn txn(blobfs_);
    uint64_t start = inode_.start_block + DataStartBlock(blobfs_->info_);
    uint64_t merkle_blocks = MerkleTreeBlocks(inode_);

    fzl::OwnedVmoMapper compressed_mapper;
    size_t compressed_blocks = (inode_.num_blocks - merkle_blocks);
    size_t compressed_size;
    if (mul_overflow(compressed_blocks, kBlobfsBlockSize, &compressed_size)) {
        FS_TRACE_ERROR("Multiplication overflow\n");
        return ZX_ERR_OUT_OF_RANGE;
    }
    zx_status_t status = compressed_mapper.CreateAndMap(compressed_size, "compressed-blob");
    if (status != ZX_OK) {
        FS_TRACE_ERROR("Failed to initialized compressed vmo; error: %d\n", status);
        return status;
    }
    vmoid_t compressed_vmoid;
    status = blobfs_->AttachVmo(compressed_mapper.vmo().get(), &compressed_vmoid);
    if (status != ZX_OK) {
        FS_TRACE_ERROR("Failed to attach commpressed VMO to blkdev: %d\n", status);
        return status;
    }

    auto detach = fbl::MakeAutoCall([this, &compressed_vmoid]() {
        blobfs_->DetachVmo(compressed_vmoid);
    });

    // Read the uncompressed merkle tree.
    txn.Enqueue(vmoid_, 0, start, merkle_blocks);
    // Read the compressed data.
    txn.Enqueue(compressed_vmoid, 0, start + merkle_blocks, compressed_blocks);

    if ((status = txn.Transact()) != ZX_OK) {
        FS_TRACE_ERROR("Failed to flush read transaction: %d\n", status);
        return status;
    }

    fs::Duration read_time = ticker.End();
    ticker.Reset();

    // Decompress the compressed data into the target buffer.
    size_t target_size = inode_.blob_size;
    status = Decompressor::Decompress(GetData(), &target_size,
                                      compressed_mapper.start(), &compressed_size);
    if (status != ZX_OK) {
        FS_TRACE_ERROR("Failed to decompress data: %d\n", status);
        return status;
    } else if (target_size != inode_.blob_size) {
        FS_TRACE_ERROR("Failed to fully decompress blob (%zu of %zu expected)\n",
                       target_size, inode_.blob_size);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    blobfs_->UpdateMerkleDecompressMetrics((compressed_blocks) * kBlobfsBlockSize,
                                           inode_.blob_size, read_time, ticker.End());
    return ZX_OK;
}

zx_status_t VnodeBlob::InitUncompressed() {
    TRACE_DURATION("blobfs", "Blobfs::InitUncompressed", "size", inode_.blob_size,
                   "blocks", inode_.num_blocks);
    fs::Ticker ticker(blobfs_->CollectingMetrics());
    fs::ReadTxn txn(blobfs_);
    uint64_t start = inode_.start_block + DataStartBlock(blobfs_->info_);

    // Read both the uncompressed merkle tree and data.
    uint64_t length = BlobDataBlocks(inode_) + MerkleTreeBlocks(inode_);
    txn.Enqueue(vmoid_, 0, start, length);
    zx_status_t status = txn.Transact();
    blobfs_->UpdateMerkleDiskReadMetrics(length * kBlobfsBlockSize, ticker.End());
    return status;
}

void VnodeBlob::PopulateInode(size_t node_index) {
    ZX_DEBUG_ASSERT(map_index_ == 0);
    ZX_DEBUG_ASSERT(inode_.start_block < kStartBlockMinimum);
    SetState(kBlobStateReadable);
    map_index_ = node_index;
    Inode* inode = blobfs_->GetNode(node_index);
    inode_ = *inode;
}

uint64_t VnodeBlob::SizeData() const {
    if (GetState() == kBlobStateReadable) {
        return inode_.blob_size;
    }
    return 0;
}

VnodeBlob::VnodeBlob(Blobfs* bs, const Digest& digest)
    : blobfs_(bs),
      flags_(kBlobStateEmpty), syncing_(false), clone_watcher_(this) {
    digest.CopyTo(digest_, sizeof(digest_));
}

VnodeBlob::VnodeBlob(Blobfs* bs)
    : blobfs_(bs),
      flags_(kBlobStateEmpty | kBlobFlagDirectory),
      syncing_(false), clone_watcher_(this) {}

void VnodeBlob::BlobCloseHandles() {
    mapping_.Reset();
    readable_event_.reset();
}

zx_status_t VnodeBlob::SpaceAllocate(uint64_t size_data) {
    TRACE_DURATION("blobfs", "Blobfs::SpaceAllocate", "size_data", size_data);
    fs::Ticker ticker(blobfs_->CollectingMetrics());

    if (GetState() != kBlobStateEmpty) {
        return ZX_ERR_BAD_STATE;
    }

    // Find a free node, mark it as reserved.
    zx_status_t status;
    if ((status = blobfs_->ReserveNode(&map_index_)) != ZX_OK) {
        return status;
    }

    // Initialize the inode with known fields
    memset(inode_.merkle_root_hash, 0, Digest::kLength);
    inode_.blob_size = size_data;
    inode_.num_blocks = MerkleTreeBlocks(inode_) + BlobDataBlocks(inode_);

    // Special case for the null blob: We skip the write phase
    if (inode_.blob_size == 0) {
        // Toss a valid block to the null blob, to distinguish it from
        // unallocated nodes.
        inode_.start_block = kStartBlockMinimum;
        if ((status = Verify()) != ZX_OK) {
            return status;
        }
        SetState(kBlobStateDataWrite);
        if ((status = WriteMetadata()) != ZX_OK) {
            fprintf(stderr, "Null blob metadata fail: %d\n", status);
            goto fail;
        }

        return ZX_OK;
    }

    // Open VMOs, so we can begin writing after allocate succeeds.
    if ((status = mapping_.CreateAndMap(inode_.num_blocks * kBlobfsBlockSize, "blob")) != ZX_OK) {
        goto fail;
    }
    if ((status = blobfs_->AttachVmo(mapping_.vmo().get(), &vmoid_)) != ZX_OK) {
        goto fail;
    }

    // Reserve space for the blob.
    if ((status = blobfs_->ReserveBlocks(inode_.num_blocks, &inode_.start_block)) != ZX_OK) {
        goto fail;
    }

    write_info_ = fbl::make_unique<WritebackInfo>();
    if (inode_.blob_size >= kCompressionMinBytesSaved) {
        size_t max = write_info_->compressor.BufferMax(inode_.blob_size);
        status = write_info_->compressed_blob.CreateAndMap(max, "compressed-blob");
        if (status != ZX_OK) {
            return status;
        }
        status = write_info_->compressor.Initialize(write_info_->compressed_blob.start(),
                                                    write_info_->compressed_blob.size());
        if (status != ZX_OK) {
            fprintf(stderr, "blobfs: Failed to initialize compressor: %d\n", status);
            return status;
        }
    }

    SetState(kBlobStateDataWrite);
    blobfs_->UpdateAllocationMetrics(size_data, ticker.End());
    return ZX_OK;

fail:
    BlobCloseHandles();
    blobfs_->FreeNode(nullptr, map_index_);
    return status;
}

void* VnodeBlob::GetData() const {
    return fs::GetBlock(kBlobfsBlockSize, mapping_.start(), MerkleTreeBlocks(inode_));
}

void* VnodeBlob::GetMerkle() const {
    return mapping_.start();
}

zx_status_t VnodeBlob::WriteMetadata() {
    TRACE_DURATION("blobfs", "Blobfs::WriteMetadata");
    assert(GetState() == kBlobStateDataWrite);

    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = blobfs_->CreateWork(&wb, this)) != ZX_OK) {
        return status;
    }

    // Update the on-disk hash.
    memcpy(inode_.merkle_root_hash, &digest_[0], Digest::kLength);

    // All data has been written to the containing VMO.
    SetState(kBlobStateReadable);
    if (readable_event_.is_valid()) {
        status = readable_event_.signal(0u, ZX_USER_SIGNAL_0);
        if (status != ZX_OK) {
            SetState(kBlobStateError);
            return status;
        }
    }

    atomic_store(&syncing_, true);

    // Allocate and persist previously reserved blocks/node.
    if (inode_.blob_size) {
        blobfs_->PersistBlocks(wb.get(), inode_.num_blocks, inode_.start_block);
    }

    blobfs_->PersistNode(wb.get(), map_index_, inode_);
    wb->SetSyncComplete();
    if ((status = blobfs_->EnqueueWork(fbl::move(wb), EnqueueType::kJournal)) != ZX_OK) {
        return status;
    }

    // Drop the write info, since we no longer need it.
    write_info_.reset();
    return status;
}

zx_status_t VnodeBlob::WriteInternal(const void* data, size_t len, size_t* actual) {
    TRACE_DURATION("blobfs", "Blobfs::WriteInternal", "data", data, "len", len);

    *actual = 0;
    if (len == 0) {
        return ZX_OK;
    }

    const uint64_t merkle_blocks = MerkleTreeBlocks(inode_);
    const size_t merkle_bytes = MerkleTreeBlocks(inode_) * kBlobfsBlockSize;
    if (GetState() == kBlobStateDataWrite) {
        size_t to_write = fbl::min(len, inode_.blob_size - write_info_->bytes_written);
        size_t offset = write_info_->bytes_written + merkle_bytes;
        zx_status_t status = mapping_.vmo().write(data, offset, to_write);
        if (status != ZX_OK) {
            return status;
        }

        *actual = to_write;
        write_info_->bytes_written += to_write;

        if (write_info_->compressor.Compressing()) {
            if ((status = write_info_->compressor.Update(data, to_write)) != ZX_OK) {
                return status;
            }
            ConsiderCompressionAbort();
        }

        // More data to write.
        if (write_info_->bytes_written < inode_.blob_size) {
            return ZX_OK;
        }

        // Only write data to disk once we've buffered the file into memory.
        // This gives us a chance to try compressing the blob before we write it back.
        fbl::unique_ptr<WritebackWork> wb;
        if ((status = blobfs_->CreateWork(&wb, this)) != ZX_OK) {
            return status;
        }

        // In case the operation fails, forcibly reset the WritebackWork
        // to avoid asserting that no write requests exist on destruction.
        auto set_error = fbl::MakeAutoCall([&]() {
            if (wb != nullptr) {
                wb->Reset(ZX_ERR_BAD_STATE);
            }

            SetState(kBlobStateError);
        });

        if (write_info_->compressor.Compressing()) {
            if ((status = write_info_->compressor.End()) != ZX_OK) {
                return status;
            }
            ConsiderCompressionAbort();
        }

        uint64_t dev_offset = DataStartBlock(blobfs_->info_) + inode_.start_block + merkle_blocks;
        if (write_info_->compressor.Compressing()) {
            uint64_t blocks = fbl::round_up(write_info_->compressor.Size(),
                                            kBlobfsBlockSize) / kBlobfsBlockSize;
            if ((status = EnqueuePaginated(&wb, blobfs_, this,
                                           write_info_->compressed_blob.vmo().get(),
                                           0, dev_offset, blocks)) != ZX_OK) {
                return status;
            }
            blocks += MerkleTreeBlocks(inode_);
            ZX_DEBUG_ASSERT(inode_.num_blocks > blocks);
            blobfs_->UnreserveBlocks(inode_.num_blocks - blocks,
                                     inode_.start_block + blocks);
            inode_.num_blocks = blocks;
            inode_.flags |= kBlobFlagLZ4Compressed;
        } else {
            uint64_t blocks = fbl::round_up(inode_.blob_size, kBlobfsBlockSize) / kBlobfsBlockSize;
            if ((status = EnqueuePaginated(&wb, blobfs_, this, mapping_.vmo().get(),
                                           merkle_blocks, dev_offset, blocks)) != ZX_OK) {
                return status;
            }
        }

        // TODO(smklein): As an optimization, use the CreateInit/Update/Final
        // methods to create the merkle tree as we write data, rather than
        // waiting until the data is fully downloaded to create the tree.
        size_t merkle_size = MerkleTree::GetTreeLength(inode_.blob_size);
        fs::Duration generation_time;
        if (merkle_size > 0) {
            Digest digest;
            void* merkle_data = GetMerkle();
            const void* blob_data = GetData();
            fs::Ticker ticker(blobfs_->CollectingMetrics()); // Tracking generation time.

            if ((status = MerkleTree::Create(blob_data, inode_.blob_size, merkle_data,
                                             merkle_size, &digest)) != ZX_OK) {
                return status;
            } else if (digest != digest_) {
                // Downloaded blob did not match provided digest.
                return ZX_ERR_IO_DATA_INTEGRITY;
            }

            uint64_t dev_offset = DataStartBlock(blobfs_->info_) + inode_.start_block;
            wb->Enqueue(mapping_.vmo().get(), 0, dev_offset, merkle_blocks);
            generation_time = ticker.End();
        } else if ((status = Verify()) != ZX_OK) {
            // Small blobs may not have associated Merkle Trees, and will
            // require validation, since we are not regenerating and checking
            // the digest.
            return status;
        }

        // Enqueue the blob's final data work. Metadata must be enqueued separately.
        if ((status = blobfs_->EnqueueWork(fbl::move(wb), EnqueueType::kData)) != ZX_OK) {
            return status;
        }

        // No more data to write. Flush to disk.
        fs::Ticker ticker(blobfs_->CollectingMetrics()); // Tracking enqueue time.
        if ((status = WriteMetadata()) != ZX_OK) {
            return status;
        }

        blobfs_->UpdateClientWriteMetrics(to_write, merkle_size, ticker.End(),
                                          generation_time);
        set_error.cancel();
        return ZX_OK;
    }

    return ZX_ERR_BAD_STATE;
}

void VnodeBlob::ConsiderCompressionAbort() {
    ZX_DEBUG_ASSERT(write_info_->compressor.Compressing());
    if (inode_.blob_size - kCompressionMinBytesSaved < write_info_->compressor.Size()) {
        write_info_->compressor.Reset();
        write_info_->compressed_blob.Reset();
    }
}

zx_status_t VnodeBlob::GetReadableEvent(zx_handle_t* out) {
    TRACE_DURATION("blobfs", "Blobfs::GetReadableEvent");
    zx_status_t status;
    // This is the first 'wait until read event' request received.
    if (!readable_event_.is_valid()) {
        status = zx::event::create(0, &readable_event_);
        if (status != ZX_OK) {
            return status;
        } else if (GetState() == kBlobStateReadable) {
            readable_event_.signal(0u, ZX_USER_SIGNAL_0);
        }
    }
    status = zx_handle_duplicate(readable_event_.get(), ZX_RIGHTS_BASIC | ZX_RIGHT_READ, out);
    if (status != ZX_OK) {
        return status;
    }
    return sizeof(zx_handle_t);
}

zx_status_t VnodeBlob::CloneVmo(zx_rights_t rights, zx_handle_t* out) {
    TRACE_DURATION("blobfs", "Blobfs::CloneVmo", "rights", rights, "out", out);
    if (GetState() != kBlobStateReadable) {
        return ZX_ERR_BAD_STATE;
    }
    if (inode_.blob_size == 0) {
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = InitVmos();
    if (status != ZX_OK) {
        return status;
    }

    // TODO(smklein): Only clone / verify the part of the vmo that
    // was requested.
    const size_t merkle_bytes = MerkleTreeBlocks(inode_) * kBlobfsBlockSize;
    zx::vmo clone;
    if ((status = mapping_.vmo().clone(ZX_VMO_CLONE_COPY_ON_WRITE, merkle_bytes, inode_.blob_size,
                                       &clone)) != ZX_OK) {
        return status;
    }

    // TODO(mdempsky): Push elsewhere.
    if ((status = clone.replace_as_executable(zx::handle(), &clone)) != ZX_OK) {
        return status;
    }

    if ((status = clone.replace(rights, &clone)) != ZX_OK) {
        return status;
    }
    *out = clone.release();

    if (clone_watcher_.object() == ZX_HANDLE_INVALID) {
        clone_watcher_.set_object(mapping_.vmo().get());
        clone_watcher_.set_trigger(ZX_VMO_ZERO_CHILDREN);

        // Keep a reference to "this" alive, preventing the blob
        // from being closed while someone may still be using the
        // underlying memory.
        //
        // We'll release it when no client-held VMOs are in use.
        clone_ref_ = fbl::RefPtr<VnodeBlob>(this);
        clone_watcher_.Begin(blobfs_->dispatcher());
    }

    return ZX_OK;
}

void VnodeBlob::HandleNoClones(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                               zx_status_t status, const zx_packet_signal_t* signal) {
    ZX_DEBUG_ASSERT(status == ZX_OK);
    ZX_DEBUG_ASSERT((signal->observed & ZX_VMO_ZERO_CHILDREN) != 0);
    ZX_DEBUG_ASSERT(clone_watcher_.object() != ZX_HANDLE_INVALID);
    clone_watcher_.set_object(ZX_HANDLE_INVALID);
    clone_ref_ = nullptr;
}

zx_status_t VnodeBlob::ReadInternal(void* data, size_t len, size_t off, size_t* actual) {
    TRACE_DURATION("blobfs", "Blobfs::ReadInternal", "len", len, "off", off);

    if (GetState() != kBlobStateReadable) {
        return ZX_ERR_BAD_STATE;
    }

    if (inode_.blob_size == 0) {
        *actual = 0;
        return ZX_OK;
    }

    zx_status_t status = InitVmos();
    if (status != ZX_OK) {
        return status;
    }

    Digest d;
    d = reinterpret_cast<const uint8_t*>(&digest_[0]);

    if (off >= inode_.blob_size) {
        *actual = 0;
        return ZX_OK;
    }
    if (len > (inode_.blob_size - off)) {
        len = inode_.blob_size - off;
    }

    const size_t merkle_bytes = MerkleTreeBlocks(inode_) * kBlobfsBlockSize;
    status = mapping_.vmo().read(data, merkle_bytes + off, len);
    if (status == ZX_OK) {
        *actual = len;
    }
    return status;
}

zx_status_t VnodeBlob::QueueUnlink() {
    flags_ |= kBlobFlagDeletable;
    // Attempt to purge in case the blob has been unlinked with no open fds
    return TryPurge();
}

zx_status_t VnodeBlob::VerifyBlob(Blobfs* bs, size_t node_index) {
    Inode* inode = bs->GetNode(node_index);
    Digest digest(inode->merkle_root_hash);
    fbl::AllocChecker ac;
    fbl::RefPtr<VnodeBlob> vn =
        fbl::AdoptRef(new (&ac) VnodeBlob(bs, digest));

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    vn->PopulateInode(node_index);

    // Set blob state to "Purged" so we do not try to add it to the cached map on recycle.
    vn->SetState(kBlobStatePurged);

    // If we are unable to read in the blob from disk, this should also be a VerifyBlob error.
    // Since InitVmos calls Verify as its final step, we can just return its result here.
    return vn->InitVmos();
}

zx_status_t Blobfs::VerifyBlob(size_t node_index) {
    return VnodeBlob::VerifyBlob(this, node_index);
}

zx_status_t Blobfs::FindBlocks(size_t start, size_t num_blocks, size_t* blkno_out) {
    while (true) {
        // Search for a range of nblocks in block_map_.
        size_t block_num;
        zx_status_t status = block_map_.Find(false, start, block_map_.size(), num_blocks,
                                             &block_num);

        if (status != ZX_OK) {
            return status;
        }

        // Find out how large the unallocated range is starting from |block_num| so we can search
        // the reserved_blocks_ map for this entire range in one call.
        size_t upper_limit = block_map_.size();
        block_map_.Scan(block_num, block_map_.size(), false, &upper_limit);
        size_t max_len = upper_limit - block_num;

        // Check the reserved map to see if there are |nblocks| free blocks from |block_num| to
        // |block_num + max_len|.
        size_t out;
        status = reserved_blocks_.Find(false, block_num, block_num + max_len, num_blocks, &out);

        // If we found a valid range, return; otherwise start searching from block_num + max_len.
        if (status == ZX_OK && out < block_num + max_len) {
            *blkno_out = out;
            break;
        }

        start = out;
    }
    return ZX_OK;
}

zx_status_t Blobfs::ReserveBlocks(size_t num_blocks, size_t* block_index_out) {
    zx_status_t status;
    if ((status = FindBlocks(0, num_blocks, block_index_out) != ZX_OK)) {
        // If we have run out of blocks, attempt to add block slices via FVM.
        // The new 'hint' is the first location we could try to find blocks
        // after merely extending the allocation maps.
        size_t hint = block_map_.size() - fbl::min(num_blocks, block_map_.size());

        if ((status = AddBlocks(num_blocks) != ZX_OK) ||
            (status = FindBlocks(hint, num_blocks, block_index_out)) != ZX_OK) {
            LogAllocationFailure(num_blocks);
            return ZX_ERR_NO_SPACE;
        }
    }

    status = reserved_blocks_.Set(*block_index_out, *block_index_out + num_blocks);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    return ZX_OK;
}

void Blobfs::LogAllocationFailure(size_t num_blocks) const {
    const uint64_t requested_bytes = num_blocks * info_.block_size;
    const uint64_t total_bytes = info_.data_block_count * info_.block_size;
    const uint64_t persisted_used_bytes = info_.alloc_block_count * info_.block_size;
    const uint64_t pending_used_bytes = reserved_blocks_.num_bits() * info_.block_size;
    const uint64_t used_bytes = persisted_used_bytes + pending_used_bytes;
    ZX_ASSERT_MSG(used_bytes <= total_bytes,
                  "blobfs using more bytes than available: %" PRIu64" > %" PRIu64"\n",
                  used_bytes, total_bytes);
    const uint64_t free_bytes = total_bytes - used_bytes;

    fprintf(stderr, "Blobfs has run out of space on persistent storage.\n");
    fprintf(stderr, "    Could not allocate %" PRIu64 " bytes\n", requested_bytes);
    fprintf(stderr, "    Total data bytes  : %" PRIu64 "\n", total_bytes);
    fprintf(stderr, "    Used data bytes   : %" PRIu64 "\n", persisted_used_bytes);
    fprintf(stderr, "    Preallocated bytes: %" PRIu64 "\n", pending_used_bytes);
    fprintf(stderr, "    Free data bytes   : %" PRIu64 "\n", free_bytes);
    fprintf(stderr, "    This allocation failure is the result of %s.\n",
            requested_bytes <= free_bytes ? "fragmentation" : "over-allocation");
}

void Blobfs::UnreserveBlocks(size_t num_blocks, size_t block_index) {
    // Ensure the blocks are already reserved.
    size_t blkno_out;
    ZX_DEBUG_ASSERT(reserved_blocks_.Find(true, block_index, block_index +
                                          num_blocks, num_blocks, &blkno_out) ==
                    ZX_OK);

    zx_status_t status = reserved_blocks_.Clear(block_index, block_index + num_blocks);
    ZX_DEBUG_ASSERT(status == ZX_OK);
}

void Blobfs::PersistBlocks(WritebackWork* wb, size_t num_blocks, size_t block_index) {
    TRACE_DURATION("blobfs", "Blobfs::PersistBlocks", "num_blocks", num_blocks);

    size_t blkno_out;
    // Make sure that blkno + nblocks are already reserved.
    ZX_DEBUG_ASSERT(reserved_blocks_.Find(true, block_index, block_index + num_blocks, num_blocks,
                                          &blkno_out) == ZX_OK);

    // Make sure that blkno + nblocks are NOT already allocated.
    ZX_DEBUG_ASSERT(block_map_.Find(false, block_index, block_index + num_blocks, num_blocks,
                                    &blkno_out) == ZX_OK);

    // Allocate blocks in bitmap.
    zx_status_t status = block_map_.Set(block_index, block_index + num_blocks);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    info_.alloc_block_count += num_blocks;

    status = reserved_blocks_.Clear(block_index, block_index + num_blocks);
    ZX_DEBUG_ASSERT(status == ZX_OK);

    // Write out to disk.
    WriteBitmap(wb, num_blocks, block_index);
    WriteInfo(wb);
}

// Frees blocks from reserved and allocated maps, updates disk in the latter case.
void Blobfs::FreeBlocks(WritebackWork* wb, size_t num_blocks, size_t block_index) {
    TRACE_DURATION("blobfs", "Blobfs::FreeBlocks", "nblocks", num_blocks, "blkno", block_index);

    // Check if blocks were allocated on disk.
    size_t blkno_out;
    if (block_map_.Find(true, block_index, block_index + num_blocks, num_blocks, &blkno_out)
        == ZX_OK) {
        zx_status_t status = block_map_.Clear(block_index, block_index + num_blocks);
        ZX_DEBUG_ASSERT(status == ZX_OK);
        info_.alloc_block_count -= num_blocks;
        WriteBitmap(wb, num_blocks, block_index);
        WriteInfo(wb);
    }

    zx_status_t status = reserved_blocks_.Clear(block_index, block_index + num_blocks);
    ZX_DEBUG_ASSERT(status == ZX_OK);
}

zx_status_t Blobfs::FindNode(size_t* node_index_out) {
    for (size_t i = free_node_lower_bound_; i < info_.inode_count; ++i) {
        if (GetNode(i)->start_block == kStartBlockFree) {
            // Found a free node. Mark it as reserved so no one else can allocate it.
            if (!reserved_nodes_.Get(i, i + 1, nullptr)) {
                reserved_nodes_.Set(i, i + 1);
                *node_index_out = i;

                // We don't know where the next free node is but we know that there
                // are no free nodes until index i.
                free_node_lower_bound_ = i + 1;
                return ZX_OK;
            }
        }
    }

    // There are no free nodes available. Setting free_node_lower_bound_ to
    // inodes_count will help to fail fast for next allocation. This will
    // also help to find nodes if nodes are added.
    free_node_lower_bound_ = info_.inode_count;

    return ZX_ERR_OUT_OF_RANGE;
}

// Reserves a node IN MEMORY.
zx_status_t Blobfs::ReserveNode(size_t* node_index_out) {
    TRACE_DURATION("blobfs", "Blobfs::ReserveNode");
    zx_status_t status;
    if ((status = FindNode(node_index_out)) == ZX_OK) {
        return ZX_OK;
    }

    // If we didn't find any free inodes, try adding more via FVM.
    if (AddInodes() != ZX_OK) {
        return ZX_ERR_NO_SPACE;
    }

    if ((status = FindNode(node_index_out)) == ZX_OK) {
        return ZX_OK;
    }

    return ZX_ERR_NO_SPACE;
}

void Blobfs::PersistNode(WritebackWork* wb, size_t node_index, const Inode& inode) {
    TRACE_DURATION("blobfs", "Blobfs::AllocateNode");

    ZX_DEBUG_ASSERT(inode.start_block >= kStartBlockMinimum);
    Inode* mapped_inode = GetNode(node_index);
    ZX_DEBUG_ASSERT(mapped_inode->start_block < kStartBlockMinimum);

    size_t blkno_out;
    ZX_DEBUG_ASSERT(reserved_nodes_.Find(true, node_index, node_index + 1, 1, &blkno_out) == ZX_OK);

    *mapped_inode = inode;
    info_.alloc_inode_count++;

    zx_status_t status = reserved_nodes_.Clear(node_index, node_index + 1);
    ZX_DEBUG_ASSERT(status == ZX_OK);

    WriteNode(wb, node_index);
    WriteInfo(wb);
}

void Blobfs::FreeNode(WritebackWork* wb, size_t node_index) {
    TRACE_DURATION("blobfs", "Blobfs::FreeNode", "node_index", node_index);
    Inode* mapped_inode = GetNode(node_index);

    // Write to disk if node has been allocated within inode table
    if (mapped_inode->start_block >= kStartBlockMinimum) {
        ZX_DEBUG_ASSERT(wb != nullptr);
        *mapped_inode = {};
        info_.alloc_inode_count--;
        WriteNode(wb, node_index);
        WriteInfo(wb);
    }

    // We update lower bound if the freed node is the smallest free node.
    if (free_node_lower_bound_ > node_index) {
        free_node_lower_bound_ = node_index;
    }
    zx_status_t status = reserved_nodes_.Clear(node_index, node_index + 1);
    ZX_DEBUG_ASSERT(status == ZX_OK);
}

zx_status_t Blobfs::InitializeWriteback(const MountOptions& options) {
    if (options.readonly) {
        // If blobfs should be readonly, do not start up any writeback threads.
        return ZX_OK;
    }

    // Initialize the WritebackQueue.
    zx_status_t status = WritebackQueue::Create(this, WriteBufferSize() / kBlobfsBlockSize,
                                                &writeback_);

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

size_t Blobfs::WritebackCapacity() const { return writeback_->GetCapacity(); }

void Blobfs::Shutdown(fs::Vfs::ShutdownCallback cb) {
    TRACE_DURATION("blobfs", "Blobfs::Unmount");

    // 1) Shutdown all external connections to blobfs.
    ManagedVfs::Shutdown([this, cb = fbl::move(cb)](zx_status_t status) mutable {
        // 2a) Shutdown all internal connections to blobfs.
        // Store the Vnodes in a vector to avoid destroying
        // them while holding the hash lock.
        fbl::Vector<fbl::RefPtr<VnodeBlob>> internal_references;
        {
            fbl::AutoLock lock(&hash_lock_);
            for (auto& blob : open_hash_) {
                auto vn = blob.CloneWatcherTeardown();
                if (vn != nullptr) {
                    internal_references.push_back(fbl::move(vn));
                }
            }
        }
        internal_references.reset();

        // 2b) Flush all pending work to blobfs to the underlying storage.
        Sync([this, cb = fbl::move(cb)](zx_status_t status) mutable {
            async::PostTask(dispatcher(), [this, cb = fbl::move(cb)]() mutable {
                // 3) Ensure the underlying disk has also flushed.
                {
                    fs::WriteTxn sync_txn(this);
                    sync_txn.EnqueueFlush();
                    sync_txn.Transact();
                    // Although the transaction shouldn't reference 'this'
                    // after completing, scope it here to be extra cautious.
                }

                DumpMetrics();

                auto on_unmount = fbl::move(on_unmount_);

                // Manually destroy Blobfs. The promise of Shutdown is that no
                // connections are active, and destroying the Blobfs object
                // should terminate all background workers.
                delete this;

                // Identify to the unmounting channel that we've completed teardown.
                cb(ZX_OK);

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
    TRACE_DURATION("blobfs", "Blobfs::WriteBitmap", "nblocks", nblocks, "start_block",
                   start_block);
    uint64_t bbm_start_block = start_block / kBlobfsBlockBits;
    uint64_t bbm_end_block = fbl::round_up(start_block + nblocks,
                                           kBlobfsBlockBits) /
                             kBlobfsBlockBits;

    // Write back the block allocation bitmap
    wb->Enqueue(block_map_.StorageUnsafe()->GetVmo(), bbm_start_block,
                BlockMapStartBlock(info_) + bbm_start_block, bbm_end_block - bbm_start_block);
}

void Blobfs::WriteNode(WritebackWork* wb, size_t map_index) {
    TRACE_DURATION("blobfs", "Blobfs::WriteNode", "map_index", map_index);
    uint64_t b = (map_index * sizeof(Inode)) / kBlobfsBlockSize;
    wb->Enqueue(node_map_.vmo().get(), b, NodeMapStartBlock(info_) + b, 1);
}

zx_status_t Blobfs::NewBlob(const Digest& digest, fbl::RefPtr<VnodeBlob>* out) {
    TRACE_DURATION("blobfs", "Blobfs::NewBlob");
    zx_status_t status;
    // If the blob already exists (or we're having trouble looking up the blob),
    // return an error.
    if ((status = LookupBlob(digest, nullptr)) != ZX_ERR_NOT_FOUND) {
        return (status == ZX_OK) ? ZX_ERR_ALREADY_EXISTS : status;
    }

    fbl::AllocChecker ac;
    *out = fbl::AdoptRef(new (&ac) VnodeBlob(this, digest));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    fbl::AutoLock lock(&hash_lock_);
    open_hash_.insert(out->get());
    return ZX_OK;
}

// If no client references to the blob still exist and the blob is either queued for deletion or
// not in a readable state, purge all traces of the blob from blobfs.
// This is only called when we do not expect the blob to be accessed again.
zx_status_t Blobfs::PurgeBlob(VnodeBlob* vn) {
    TRACE_DURATION("blobfs", "Blobfs::PurgeBlob");

    switch (vn->GetState()) {
    case kBlobStateEmpty: {
        VnodeReleaseHard(vn);
        return ZX_OK;
    }
    case kBlobStateReadable: {
        // A readable blob should only be purged if it has been unlinked
        ZX_ASSERT(vn->DeletionQueued());
        __FALLTHROUGH;
    }
    case kBlobStateDataWrite:
    case kBlobStateError: {
        size_t node_index = vn->GetMapIndex();
        uint64_t start_block = vn->GetNode().start_block;
        uint64_t nblocks = vn->GetNode().num_blocks;
        zx_status_t status;
        fbl::unique_ptr<WritebackWork> wb;
        if ((status = CreateWork(&wb, vn)) != ZX_OK) {
            return status;
        }

        FreeNode(wb.get(), node_index);
        FreeBlocks(wb.get(), nblocks, start_block);
        VnodeReleaseHard(vn);
        return EnqueueWork(fbl::move(wb), EnqueueType::kJournal);
    }
    default: {
        assert(false);
    }
    }
    return ZX_ERR_NOT_SUPPORTED;
}

void Blobfs::WriteInfo(WritebackWork* wb) {
    memcpy(info_mapping_.start(), &info_, sizeof(info_));
    wb->Enqueue(info_mapping_.vmo().get(), 0, 0, 1);
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
        if (GetNode(i)->start_block >= kStartBlockMinimum) {
            Digest digest(GetNode(i)->merkle_root_hash);
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

zx_status_t Blobfs::LookupBlob(const Digest& digest, fbl::RefPtr<VnodeBlob>* out) {
    TRACE_DURATION("blobfs", "Blobfs::LookupBlob");
    const uint8_t* key = digest.AcquireBytes();
    auto release = fbl::MakeAutoCall([&digest]() {
        digest.ReleaseBytes();
    });

    // Look up the blob in the maps.
    fbl::RefPtr<VnodeBlob> vn;
    while (true) {
        // Avoid releasing a reference to |vn| while holding |hash_lock_|.
        fbl::AutoLock lock(&hash_lock_);
        auto raw_vn = open_hash_.find(key).CopyPointer();
        if (raw_vn != nullptr) {
            vn = fbl::MakeRefPtrUpgradeFromRaw(raw_vn, hash_lock_);
            if (vn == nullptr) {
                // This condition is only possible if:
                // - The raw pointer to the Vnode exists in the open map,
                // with refcount == 0.
                // - Another thread is fbl_recycling this Vnode, but has not
                // yet resurrected it.
                // - The vnode is being moved to the close cache, and is
                // not yet purged.
                //
                // It is not safe for us to attempt to Resurrect the Vnode. If
                // we do so, then the caller of LookupBlob may unlink, purge, and
                // destroy the Vnode concurrently before the original caller of
                // "fbl_recycle" completes.
                //
                // Since the window of time for this condition is extremely
                // small (between Release and the resurrection of the Vnode),
                // and only contains a single flag check, we unlock and try
                // again.
                continue;
            }
        } else {
            vn = VnodeUpgradeLocked(key);
        }
        break;
    }

    if (vn != nullptr) {
        UpdateLookupMetrics(vn->SizeData());
        if (out != nullptr) {
            *out = fbl::move(vn);
        }
        return ZX_OK;
    }

    return ZX_ERR_NOT_FOUND;
}

zx_status_t Blobfs::AttachVmo(zx_handle_t vmo, vmoid_t* out) {
    zx_handle_t xfer_vmo;
    zx_status_t status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &xfer_vmo);
    if (status != ZX_OK) {
        return status;
    }
    ssize_t r = ioctl_block_attach_vmo(Fd(), &xfer_vmo, out);
    if (r < 0) {
        zx_handle_close(xfer_vmo);
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

zx_status_t Blobfs::AddInodes() {
    TRACE_DURATION("blobfs", "Blobfs::AddInodes");

    if (!(info_.flags & kBlobFlagFVM)) {
        return ZX_ERR_NO_SPACE;
    }

    const size_t kBlocksPerSlice = info_.slice_size / kBlobfsBlockSize;
    extend_request_t request;
    request.length = 1;
    request.offset = (kFVMNodeMapStart / kBlocksPerSlice) + info_.ino_slices;
    if (ioctl_block_fvm_extend(Fd(), &request) < 0) {
        fprintf(stderr, "Blobfs::AddInodes fvm_extend failure");
        return ZX_ERR_NO_SPACE;
    }

    const uint32_t kInodesPerSlice = static_cast<uint32_t>(info_.slice_size / kBlobfsInodeSize);
    uint64_t inodes64 = (info_.ino_slices + static_cast<uint32_t>(request.length)) * kInodesPerSlice;
    ZX_DEBUG_ASSERT(inodes64 <= fbl::numeric_limits<uint32_t>::max());
    uint32_t inodes = static_cast<uint32_t>(inodes64);
    uint32_t inoblks = (inodes + kBlobfsInodesPerBlock - 1) / kBlobfsInodesPerBlock;
    ZX_DEBUG_ASSERT(info_.inode_count <= fbl::numeric_limits<uint32_t>::max());
    uint32_t inoblks_old = (static_cast<uint32_t>(info_.inode_count) + kBlobfsInodesPerBlock - 1) / kBlobfsInodesPerBlock;
    ZX_DEBUG_ASSERT(inoblks_old <= inoblks);

    if (node_map_.Grow(inoblks * kBlobfsBlockSize) != ZX_OK) {
        return ZX_ERR_NO_SPACE;
    }

    info_.vslice_count += request.length;
    info_.ino_slices += static_cast<uint32_t>(request.length);
    info_.inode_count = inodes;

    // Reset new inodes to 0
    uintptr_t addr = reinterpret_cast<uintptr_t>(node_map_.start());
    memset(reinterpret_cast<void*>(addr + kBlobfsBlockSize * inoblks_old), 0,
           (kBlobfsBlockSize * (inoblks - inoblks_old)));

    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = CreateWork(&wb, nullptr)) != ZX_OK) {
        return status;
    }

    WriteInfo(wb.get());
    wb.get()->Enqueue(node_map_.vmo().get(), inoblks_old, NodeMapStartBlock(info_) + inoblks_old,
                inoblks - inoblks_old);
    return EnqueueWork(fbl::move(wb), EnqueueType::kJournal);
}

zx_status_t Blobfs::AddBlocks(size_t nblocks) {
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
    ZX_DEBUG_ASSERT(blocks64 <= fbl::numeric_limits<uint32_t>::max());
    uint32_t blocks = static_cast<uint32_t>(blocks64);
    uint32_t abmblks = (blocks + kBlobfsBlockBits - 1) / kBlobfsBlockBits;
    uint64_t abmblks_old = (info_.data_block_count + kBlobfsBlockBits - 1) / kBlobfsBlockBits;
    ZX_DEBUG_ASSERT(abmblks_old <= abmblks);

    if (abmblks > kBlocksPerSlice) {
        //TODO(planders): Allocate more slices for the block bitmap.
        fprintf(stderr, "Blobfs::AddBlocks needs to increase block bitmap size\n");
        return ZX_ERR_NO_SPACE;
    }

    if (ioctl_block_fvm_extend(Fd(), &request) < 0) {
        fprintf(stderr, "Blobfs::AddBlocks FVM Extend failure\n");
        return ZX_ERR_NO_SPACE;
    }

    // Grow the block bitmap to hold new number of blocks
    if (block_map_.Grow(fbl::round_up(blocks, kBlobfsBlockBits)) != ZX_OK) {
        return ZX_ERR_NO_SPACE;
    }
    // Grow before shrinking to ensure the underlying storage is a multiple
    // of kBlobfsBlockSize.
    block_map_.Shrink(blocks);

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
        wb.get()->Enqueue(block_map_.StorageUnsafe()->GetVmo(), vmo_offset, dev_offset, length);
    }

    info_.vslice_count += request.length;
    info_.dat_slices += static_cast<uint32_t>(request.length);
    info_.data_block_count = blocks;

    WriteInfo(wb.get());
    return EnqueueWork(fbl::move(wb), EnqueueType::kJournal);
}

void Blobfs::Sync(SyncCallback closure) {
    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = CreateWork(&wb, nullptr)) != ZX_OK) {
        closure(status);
        return;
    }

    wb->SetSyncCallback(fbl::move(closure));
    // This may return an error, but it doesn't matter - the closure will be called anyway.
    status = EnqueueWork(fbl::move(wb), EnqueueType::kJournal);
}

void Blobfs::UpdateAllocationMetrics(uint64_t size_data, const fs::Duration& duration) {
    if (CollectingMetrics()) {
        metrics_.blobs_created++;
        metrics_.blobs_created_total_size += size_data;
        metrics_.total_allocation_time_ticks += duration;
    }
}

void Blobfs::UpdateLookupMetrics(uint64_t size) {
    if (CollectingMetrics()) {
        metrics_.blobs_opened++;
        metrics_.blobs_opened_total_size += size;
    }
}

void Blobfs::UpdateClientWriteMetrics(uint64_t data_size, uint64_t merkle_size,
                                      const fs::Duration& enqueue_duration,
                                      const fs::Duration& generate_duration) {
    if (CollectingMetrics()) {
        metrics_.data_bytes_written += data_size;
        metrics_.merkle_bytes_written += merkle_size;
        metrics_.total_write_enqueue_time_ticks += enqueue_duration;
        metrics_.total_merkle_generation_time_ticks += generate_duration;
    }
}

void Blobfs::UpdateWritebackMetrics(uint64_t size, const fs::Duration& duration) {
    if (CollectingMetrics()) {
        metrics_.total_writeback_time_ticks += duration;
        metrics_.total_writeback_bytes_written += size;
    }
}

void Blobfs::UpdateMerkleDiskReadMetrics(uint64_t size, const fs::Duration& duration) {
    if (CollectingMetrics()) {
        metrics_.total_read_from_disk_time_ticks += duration;
        metrics_.bytes_read_from_disk += size;
    }
}

void Blobfs::UpdateMerkleDecompressMetrics(uint64_t size_compressed,
                                           uint64_t size_uncompressed,
                                           const fs::Duration& read_duration,
                                           const fs::Duration& decompress_duration) {
    if (CollectingMetrics()) {
        metrics_.bytes_compressed_read_from_disk += size_compressed;
        metrics_.bytes_decompressed_from_disk += size_uncompressed;
        metrics_.total_read_compressed_time_ticks += read_duration;
        metrics_.total_decompress_time_ticks += decompress_duration;
    }
}

void Blobfs::UpdateMerkleVerifyMetrics(uint64_t size_data, uint64_t size_merkle,
                                       const fs::Duration& duration) {
    if (CollectingMetrics()) {
        metrics_.blobs_verified++;
        metrics_.blobs_verified_total_size_data += size_data;
        metrics_.blobs_verified_total_size_merkle += size_merkle;
        metrics_.total_verification_time_ticks += duration;
    }
}

Blobfs::Blobfs(fbl::unique_fd fd, const Superblock* info)
    : blockfd_(fbl::move(fd)) {
    memcpy(&info_, info, sizeof(Superblock));
}

Blobfs::~Blobfs() {
    // The journal must be destroyed before the writeback buffer, since it may still need
    // to enqueue more transactions for writeback.
    journal_.reset();
    writeback_.reset();

    ZX_ASSERT(open_hash_.is_empty());
    closed_hash_.clear();

    if (blockfd_) {
        ioctl_block_fifo_close(Fd());
    }
}

zx_status_t Blobfs::Create(fbl::unique_fd fd, const MountOptions& options,
                           const Superblock* info, fbl::unique_ptr<Blobfs>* out) {
    TRACE_DURATION("blobfs", "Blobfs::Create");
    zx_status_t status = CheckSuperblock(info, TotalBlocks(*info));
    if (status < 0) {
        fprintf(stderr, "blobfs: Check info failure\n");
        return status;
    }

    fbl::AllocChecker ac;
    auto fs = fbl::unique_ptr<Blobfs>(new Blobfs(fbl::move(fd), info));
    fs->SetReadonly(options.readonly);
    fs->SetCachePolicy(options.cache_policy);
    if (options.metrics) {
        fs->CollectMetrics();
    }

    zx::fifo fifo;
    ssize_t r;
    if ((r = ioctl_block_get_info(fs->Fd(), &fs->block_info_)) < 0) {
        return static_cast<zx_status_t>(r);
    } else if (kBlobfsBlockSize % fs->block_info_.block_size != 0) {
        return ZX_ERR_IO;
    } else if ((r = ioctl_block_get_fifos(fs->Fd(), fifo.reset_and_get_address())) < 0) {
        fprintf(stderr, "Failed to mount blobfs: Someone else is using the block device\n");
        return static_cast<zx_status_t>(r);
    }

    if ((status = block_client::Client::Create(fbl::move(fifo), &fs->fifo_client_)) != ZX_OK) {
        return status;
    }

    // Keep the block_map_ aligned to a block multiple
    if ((status = fs->block_map_.Reset(BlockMapBlocks(fs->info_) * kBlobfsBlockBits)) < 0) {
        fprintf(stderr, "blobfs: Could not reset block bitmap\n");
        return status;
    } else if ((status = fs->block_map_.Shrink(fs->info_.data_block_count)) < 0) {
        fprintf(stderr, "blobfs: Could not shrink block bitmap\n");
        return status;
    }

    size_t nodemap_size = kBlobfsInodeSize * fs->info_.inode_count;
    ZX_DEBUG_ASSERT(fbl::round_up(nodemap_size, kBlobfsBlockSize) == nodemap_size);
    ZX_DEBUG_ASSERT(nodemap_size / kBlobfsBlockSize == NodeMapBlocks(fs->info_));
    if ((status = fs->node_map_.CreateAndMap(nodemap_size, "nodemap")) != ZX_OK) {
        return status;
    } else if ((status = fs->AttachVmo(fs->block_map_.StorageUnsafe()->GetVmo(),
                                       &fs->block_map_vmoid_)) != ZX_OK) {
        return status;
    } else if ((status = fs->AttachVmo(fs->node_map_.vmo().get(),
                                       &fs->node_map_vmoid_)) != ZX_OK) {
        return status;
    } else if ((status = fs->LoadBitmaps()) < 0) {
        fprintf(stderr, "blobfs: Failed to load bitmaps: %d\n", status);
        return status;
    } else if ((status = fs->info_mapping_.CreateAndMap(kBlobfsBlockSize,
                                                        "blobfs-superblock")) != ZX_OK) {
        fprintf(stderr, "blobfs: Failed to create info vmo: %d\n", status);
        return status;
    } else if ((status = fs->AttachVmo(fs->info_mapping_.vmo().get(),
                                       &fs->info_vmoid_)) != ZX_OK) {
        fprintf(stderr, "blobfs: Failed to attach info vmo: %d\n", status);
        return status;
    } else if ((status = fs->CreateFsId()) != ZX_OK) {
        fprintf(stderr, "blobfs: Failed to create fs_id: %d\n", status);
        return status;
    } else if ((status = fs->InitializeVnodes() != ZX_OK)) {
        fprintf(stderr, "blobfs: Failed to initialize Vnodes\n");
        return status;
    }

    status = Journal::Create(fs.get(), JournalBlocks(fs->info_), JournalStartBlock(fs->info_),
                             &fs->journal_);
    if (status != ZX_OK) {
        return status;
    }

    *out = fbl::move(fs);
    return ZX_OK;
}

zx_status_t Blobfs::InitializeVnodes() {
    fbl::AutoLock lock(&hash_lock_);
    closed_hash_.clear();
    for (size_t i = 0; i < info_.inode_count; ++i) {
        const Inode* inode = GetNode(i);
        if (inode->start_block >= kStartBlockMinimum) {
            fbl::AllocChecker ac;
            Digest digest(inode->merkle_root_hash);
            fbl::RefPtr<VnodeBlob> vn = fbl::AdoptRef(new (&ac) VnodeBlob(this, digest));
            if (!ac.check()) {
                return ZX_ERR_NO_MEMORY;
            }
            vn->SetState(kBlobStateReadable);
            vn->PopulateInode(i);

            // Delay reading any data from disk until read.
            size_t size = vn->SizeData();
            zx_status_t status = VnodeInsertClosedLocked(fbl::move(vn));
            if (status != ZX_OK) {
                char name[digest::Digest::kLength * 2 + 1];
                digest.ToString(name, sizeof(name));
                fprintf(stderr, "blobfs: CORRUPTED FILESYSTEM: Duplicate node: "
                        "%s @ index %zu\n", name, i);
                return status;
            }
            UpdateLookupMetrics(size);
        }
    }
    return ZX_OK;
}

void Blobfs::VnodeReleaseHard(VnodeBlob* vn) {
    fbl::AutoLock lock(&hash_lock_);
    ZX_ASSERT(open_hash_.erase(vn->GetKey()) != nullptr);
}

void Blobfs::VnodeReleaseSoft(VnodeBlob* raw_vn) {
    fbl::AutoLock lock(&hash_lock_);
    raw_vn->ResurrectRef();
    fbl::RefPtr<VnodeBlob> vn = fbl::internal::MakeRefPtrNoAdopt(raw_vn);
    ZX_ASSERT(open_hash_.erase(raw_vn->GetKey()) != nullptr);
    ZX_ASSERT(VnodeInsertClosedLocked(fbl::move(vn)) == ZX_OK);
}

zx_status_t Blobfs::Reload() {
    TRACE_DURATION("blobfs", "Blobfs::Reload");

    // Re-read the info block from disk.
    zx_status_t status;
    char block[kBlobfsBlockSize];
    if ((status = readblk(Fd(), 0, block)) != ZX_OK) {
        fprintf(stderr, "blobfs: could not read info block\n");
        return status;
    }

    Superblock* info = reinterpret_cast<Superblock*>(&block[0]);
    if ((status = CheckSuperblock(info, TotalBlocks(*info))) != ZX_OK) {
        fprintf(stderr, "blobfs: Check info failure\n");
        return status;
    }

    // Once it has been verified, overwrite the current info.
    memcpy(&info_, info, sizeof(Superblock));

    // If block map size has changed, reset the block map vmo.
    if (info_.data_block_count != block_map_.size()) {
        if ((status = block_map_.Reset(BlockMapBlocks(info_) * kBlobfsBlockBits)) != ZX_OK) {
            fprintf(stderr, "blobfs: Could not reset block bitmap\n");
            return status;
        }

        if ((status = block_map_.Shrink(info_.data_block_count)) != ZX_OK) {
            fprintf(stderr, "blobfs: Could not shrink block bitmap\n");
            return status;
        }
    }

    // If node map size has changed, grow/shrink the node map accordingly.
    size_t nodemap_size = kBlobfsInodeSize * info_.inode_count;
    ZX_DEBUG_ASSERT(fbl::round_up(nodemap_size, kBlobfsBlockSize) == nodemap_size);
    ZX_DEBUG_ASSERT(nodemap_size / kBlobfsBlockSize == NodeMapBlocks(info_));

    if (nodemap_size > node_map_.size()) {
        if ((status = node_map_.Grow(nodemap_size)) != ZX_OK) {
            fprintf(stderr, "blobfs: Failed to grow node map\n");
            return status;
        }
    } else if (nodemap_size < node_map_.size()) {
        if ((status = node_map_.Shrink(nodemap_size)) != ZX_OK) {
            fprintf(stderr, "blobfs: Failed to shrink node map\n");
            return status;
        }
    }

    // Load the bitmaps from disk.
    if ((status = LoadBitmaps()) != ZX_OK) {
        fprintf(stderr, "blobfs: Failed to load bitmaps: %d\n", status);
        return status;
    }

    // Load the vnodes from disk.
    if ((status = InitializeVnodes() != ZX_OK)) {
        fprintf(stderr, "blobfs: Failed to initialize Vnodes\n");
        return status;
    }

    return ZX_OK;
}

zx_status_t Blobfs::VnodeInsertClosedLocked(fbl::RefPtr<VnodeBlob> vn) {
    // To exist in the closed_hash_, this RefPtr must be leaked.
    if (!closed_hash_.insert_or_find(vn.get())) {
        // Set blob state to "Purged" so we do not try to add it to the cached map on recycle.
        vn->SetState(kBlobStatePurged);
        return ZX_ERR_ALREADY_EXISTS;
    }

    // While in the closed cache, the blob may either be destroyed or in an
    // inactive state. The toggles here make tradeoffs between memory usage
    // and performance.
    switch (cache_policy_) {
    case CachePolicy::EvictImmediately:
        vn->TearDown();
        break;
    case CachePolicy::NeverEvict:
        break;
    default:
        ZX_ASSERT_MSG(false, "Unexpected cache policy");
    }

    __UNUSED auto leak = vn.leak_ref();
    return ZX_OK;
}

fbl::RefPtr<VnodeBlob> Blobfs::VnodeUpgradeLocked(const uint8_t* key) {
    ZX_DEBUG_ASSERT(open_hash_.find(key).CopyPointer() == nullptr);
    VnodeBlob* raw_vn = closed_hash_.erase(key);
    if (raw_vn == nullptr) {
        return nullptr;
    }
    open_hash_.insert(raw_vn);
    // To have existed in the closed_hash_, this RefPtr must have
    // been leaked.
    return fbl::internal::MakeRefPtrNoAdopt(raw_vn);
}

zx_status_t Blobfs::OpenRootNode(fbl::RefPtr<VnodeBlob>* out) {
    fbl::AllocChecker ac;
    fbl::RefPtr<VnodeBlob> vn =
        fbl::AdoptRef(new (&ac) VnodeBlob(this));

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = vn->Open(0, nullptr);
    if (status != ZX_OK) {
        return status;
    }

    *out = fbl::move(vn);
    return ZX_OK;
}

zx_status_t Blobfs::LoadBitmaps() {
    TRACE_DURATION("blobfs", "Blobfs::LoadBitmaps");
    reserved_nodes_.ClearAll();
    fs::ReadTxn txn(this);
    txn.Enqueue(block_map_vmoid_, 0, BlockMapStartBlock(info_), BlockMapBlocks(info_));
    txn.Enqueue(node_map_vmoid_, 0, NodeMapStartBlock(info_), NodeMapBlocks(info_));
    return txn.Transact();
}

zx_status_t Initialize(fbl::unique_fd blockfd, const MountOptions& options,
                       fbl::unique_ptr<Blobfs>* out) {
    zx_status_t status;

    char block[kBlobfsBlockSize];
    if ((status = readblk(blockfd.get(), 0, (void*)block)) < 0) {
        fprintf(stderr, "blobfs: could not read info block\n");
        return status;
    }

    Superblock* info = reinterpret_cast<Superblock*>(&block[0]);

    uint64_t blocks;
    if ((status = GetBlockCount(blockfd.get(), &blocks)) != ZX_OK) {
        fprintf(stderr, "blobfs: cannot find end of underlying device\n");
        return status;
    }

    if ((status = CheckSuperblock(info, blocks)) != ZX_OK) {
        fprintf(stderr, "blobfs: Info check failed\n");
        return status;
    }

    if ((status = Blobfs::Create(fbl::move(blockfd), options, info, out)) != ZX_OK) {
        fprintf(stderr, "blobfs: mount failed; could not create blobfs\n");
        return status;
    }
    return ZX_OK;
}

zx_status_t Mount(async_dispatcher_t* dispatcher, fbl::unique_fd blockfd,
                  const MountOptions& options, zx::channel root,
                  fbl::Closure on_unmount) {
    zx_status_t status;
    fbl::unique_ptr<Blobfs> fs;

    if ((status = Initialize(fbl::move(blockfd), options, &fs)) != ZX_OK) {
        return status;
    }

    // Attempt to initialize writeback and journal.
    // The journal must be replayed before the FVM check, in case changes to slice counts have
    // been written to the journal but not persisted to the super block.
    if ((status = fs->InitializeWriteback(options)) != ZX_OK) {
        return status;
    }

    if ((status = CheckFvmConsistency(&fs->Info(), fs->Fd())) != ZX_OK) {
        fprintf(stderr, "blobfs: FVM info check failed\n");
        return status;
    }

    fs->SetDispatcher(dispatcher);
    fs->SetUnmountCallback(fbl::move(on_unmount));

    fbl::RefPtr<VnodeBlob> vn;
    if ((status = fs->OpenRootNode(&vn)) != ZX_OK) {
        fprintf(stderr, "blobfs: mount failed; could not get root blob\n");
        return status;
    }

    if ((status = fs->ServeDirectory(fbl::move(vn), fbl::move(root))) != ZX_OK) {
        fprintf(stderr, "blobfs: mount failed; could not serve root directory\n");
        return status;
    }

    // Shutdown is now responsible for deleting the Blobfs object.
    __UNUSED auto r = fs.release();
    return ZX_OK;
}

zx_status_t Blobfs::CreateWork(fbl::unique_ptr<WritebackWork>* out, VnodeBlob* vnode) {
    if (writeback_ == nullptr) {
        // Transactions should never be allowed if the writeback queue is disabled.
        return ZX_ERR_BAD_STATE;
    }

    out->reset(new WritebackWork(this, fbl::WrapRefPtr(vnode)));
    return ZX_OK;
}

zx_status_t Blobfs::EnqueueWork(fbl::unique_ptr<WritebackWork> work, EnqueueType type) {
    switch(type) {
    case EnqueueType::kJournal:
        if (journal_ != nullptr) {
            // If journaling is enabled (both in general and for this WritebackWork),
            // attempt to enqueue to the journal buffer.
            return journal_->Enqueue(fbl::move(work));
        }
        // Even if our enqueue type is kJournal,
        // fall through to the writeback queue if the journal doesn't exist.
        __FALLTHROUGH;
    case EnqueueType::kData:
        if (writeback_ != nullptr) {
            return writeback_->Enqueue(fbl::move(work));
        }
        // If writeback_ does not exist, we are in a readonly state.
        // Fall through to the default case.
        __FALLTHROUGH;
    default:
        // The file system is currently in a readonly state.
        // Reset the work to ensure that any callbacks are completed.
        work->Reset(ZX_ERR_BAD_STATE);
        return ZX_ERR_BAD_STATE;
    }
}
} // namespace blobfs
