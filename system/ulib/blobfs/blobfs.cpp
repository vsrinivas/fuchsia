// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
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

zx_status_t CheckFvmConsistency(const blobfs_info_t* info, int block_fd) {
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

    size_t expected_count[3];
    expected_count[0] = info->abm_slices;
    expected_count[1] = info->ino_slices;
    expected_count[2] = info->dat_slices;

    query_request_t request;
    request.count = 3;
    request.vslice_start[0] = kFVMBlockMapStart / kBlocksPerSlice;
    request.vslice_start[1] = kFVMNodeMapStart / kBlocksPerSlice;
    request.vslice_start[2] = kFVMDataStart / kBlocksPerSlice;

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
    const size_t kMaxChunkBlocks = (3 * kWriteBufferBlocks) / 4;
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
            blobfs->EnqueueWork(fbl::move(*work));
            *work = fbl::move(tmp);
        }
    }
    return ZX_OK;
}

}  // namespace

blobfs_inode_t* Blobfs::GetNode(size_t index) const {
    return &reinterpret_cast<blobfs_inode_t*>(node_map_->GetData())[index];
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

    if (blob_ != nullptr) {
        return ZX_OK;
    }

    // Reverts blob back to uninitialized state on error.
    auto cleanup = fbl::MakeAutoCall([this]() { BlobCloseHandles(); });

    zx_status_t status;
    uint64_t data_blocks = BlobDataBlocks(inode_);
    uint64_t merkle_blocks = MerkleTreeBlocks(inode_);
    uint64_t num_blocks = data_blocks + merkle_blocks;
    size_t vmo_size;
    if (mul_overflow(num_blocks, kBlobfsBlockSize, &vmo_size)) {
        FS_TRACE_ERROR("Multiplication overflow");
        return ZX_ERR_OUT_OF_RANGE;
    }
    if ((status = fs::MappedVmo::Create(vmo_size, "blob", &blob_)) != ZX_OK) {
        FS_TRACE_ERROR("Failed to initialize vmo; error: %d\n", status);
        return status;
    }
    if ((status = blobfs_->AttachVmo(blob_->GetVmo(), &vmoid_)) != ZX_OK) {
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
    ReadTxn txn(blobfs_);
    uint64_t start = inode_.start_block + DataStartBlock(blobfs_->info_);
    uint64_t merkle_blocks = MerkleTreeBlocks(inode_);

    fbl::unique_ptr<fs::MappedVmo> compressed_blob;
    size_t compressed_blocks = (inode_.num_blocks - merkle_blocks);
    size_t compressed_size;
    if (mul_overflow(compressed_blocks, kBlobfsBlockSize, &compressed_size)) {
        FS_TRACE_ERROR("Multiplication overflow\n");
        return ZX_ERR_OUT_OF_RANGE;
    }
    zx_status_t status = fs::MappedVmo::Create(compressed_size, "compressed-blob",
                                               &compressed_blob);
    if (status != ZX_OK) {
        FS_TRACE_ERROR("Failed to initialized compressed vmo; error: %d\n", status);
        return status;
    }
    vmoid_t compressed_vmoid;
    status = blobfs_->AttachVmo(compressed_blob->GetVmo(), &compressed_vmoid);
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

    if ((status = txn.Flush()) != ZX_OK) {
        FS_TRACE_ERROR("Failed to flush read transaction: %d\n", status);
        return status;
    }

    fs::Duration read_time = ticker.End();
    ticker.Reset();

    // Decompress the compressed data into the target buffer.
    size_t target_size = inode_.blob_size;
    status = Decompressor::Decompress(GetData(), &target_size,
                                      compressed_blob->GetData(), &compressed_size);
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
    ReadTxn txn(blobfs_);
    uint64_t start = inode_.start_block + DataStartBlock(blobfs_->info_);

    // Read both the uncompressed merkle tree and data.
    uint64_t length = BlobDataBlocks(inode_) + MerkleTreeBlocks(inode_);
    txn.Enqueue(vmoid_, 0, start, length);
    zx_status_t status = txn.Flush();
    blobfs_->UpdateMerkleDiskReadMetrics(length * kBlobfsBlockSize, ticker.End());
    return status;
}

void VnodeBlob::PopulateInode(size_t node_index) {
    ZX_DEBUG_ASSERT(map_index_ == 0);
    ZX_DEBUG_ASSERT(inode_.start_block < kStartBlockMinimum);
    SetState(kBlobStateReadable);
    map_index_ = node_index;
    blobfs_inode_t* inode = blobfs_->GetNode(node_index);
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
    blob_ = nullptr;
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
        fbl::unique_ptr<WritebackWork> wb;
        if ((status = blobfs_->CreateWork(&wb, this)) != ZX_OK) {
           return status;
        } else if ((status = WriteMetadata(fbl::move(wb))) != ZX_OK) {
            fprintf(stderr, "Null blob metadata fail: %d\n", status);
            goto fail;
        }

        return ZX_OK;
    }

    // Open VMOs, so we can begin writing after allocate succeeds.
    if ((status = fs::MappedVmo::Create(inode_.num_blocks * kBlobfsBlockSize, "blob", &blob_))
        != ZX_OK) {
        goto fail;
    }
    if ((status = blobfs_->AttachVmo(blob_->GetVmo(), &vmoid_)) != ZX_OK) {
        goto fail;
    }

    // Reserve space for the blob.
    if ((status = blobfs_->ReserveBlocks(inode_.num_blocks, &inode_.start_block)) != ZX_OK) {
        goto fail;
    }

    write_info_ = fbl::make_unique<WritebackInfo>();
    if (inode_.blob_size >= kCompressionMinBytesSaved) {
        size_t max = write_info_->compressor.BufferMax(inode_.blob_size);
        status = fs::MappedVmo::Create(max, "compressed-blob", &write_info_->compressed_blob);
        if (status != ZX_OK) {
            return status;
        }
        status = write_info_->compressor.Initialize(write_info_->compressed_blob->GetData(),
                                                    write_info_->compressed_blob->GetSize());
        if (status != ZX_OK) {
            fprintf(stderr, "blobfs: Failed to initalize compressor: %d\n", status);
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
    return fs::GetBlock<kBlobfsBlockSize>(blob_->GetData(), MerkleTreeBlocks(inode_));
}

void* VnodeBlob::GetMerkle() const {
    return blob_->GetData();
}

zx_status_t VnodeBlob::WriteMetadata(fbl::unique_ptr<WritebackWork> wb) {
    TRACE_DURATION("blobfs", "Blobfs::WriteMetadata");
    assert(GetState() == kBlobStateDataWrite);

    // Update the on-disk hash.
    memcpy(inode_.merkle_root_hash, &digest_[0], Digest::kLength);

    // All data has been written to the containing VMO.
    SetState(kBlobStateReadable);
    if (readable_event_.is_valid()) {
        zx_status_t status = readable_event_.signal(0u, ZX_USER_SIGNAL_0);
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
    blobfs_->EnqueueWork(fbl::move(wb));

    // Drop the write info, since we no longer need it.
    write_info_.reset();
    return ZX_OK;
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
        zx_status_t status = zx_vmo_write(blob_->GetVmo(), data, offset, to_write);
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
                                           write_info_->compressed_blob->GetVmo(),
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
            if ((status = EnqueuePaginated(&wb, blobfs_, this, blob_->GetVmo(),
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
                SetState(kBlobStateError);
                return status;
            } else if (digest != digest_) {
                // Downloaded blob did not match provided digest.
                SetState(kBlobStateError);
                return ZX_ERR_IO_DATA_INTEGRITY;
            }

            uint64_t dev_offset = DataStartBlock(blobfs_->info_) + inode_.start_block;
            wb->Enqueue(blob_->GetVmo(), 0, dev_offset, merkle_blocks);
            generation_time = ticker.End();
        } else if ((status = Verify()) != ZX_OK) {
            // Small blobs may not have associated Merkle Trees, and will
            // require validation, since we are not regenerating and checking
            // the digest.
            SetState(kBlobStateError);
            return status;
        }

        // No more data to write. Flush to disk.
        fs::Ticker ticker(blobfs_->CollectingMetrics()); // Tracking enqueue time.
        if ((status = WriteMetadata(fbl::move(wb))) != ZX_OK) {
            SetState(kBlobStateError);
            return status;
        }

        blobfs_->UpdateClientWriteMetrics(to_write, merkle_size, ticker.End(),
                                          generation_time);
        return ZX_OK;
    }

    return ZX_ERR_BAD_STATE;
}

void VnodeBlob::ConsiderCompressionAbort() {
    ZX_DEBUG_ASSERT(write_info_->compressor.Compressing());
    if (inode_.blob_size - kCompressionMinBytesSaved < write_info_->compressor.Size()) {
        write_info_->compressor.Reset();
        write_info_->compressed_blob = nullptr;
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
    zx_handle_t clone;
    if ((status = zx_vmo_clone(blob_->GetVmo(), ZX_VMO_CLONE_COPY_ON_WRITE,
                               merkle_bytes, inode_.blob_size, &clone)) != ZX_OK) {
        return status;
    }

    if ((status = zx_handle_replace(clone, rights, out)) != ZX_OK) {
        return status;
    }

    if (clone_watcher_.object() == ZX_HANDLE_INVALID) {
        clone_watcher_.set_object(blob_->GetVmo());
        clone_watcher_.set_trigger(ZX_VMO_ZERO_CHILDREN);

        // Keep a reference to "this" alive, preventing the blob
        // from being closed while someone may still be using the
        // underlying memory.
        //
        // We'll release it when no client-held VMOs are in use.
        clone_ref_ = fbl::RefPtr<VnodeBlob>(this);
        clone_watcher_.Begin(blobfs_->async());
    }

    return ZX_OK;
}

void VnodeBlob::HandleNoClones(async_t* async, async::WaitBase* wait,
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
    status = zx_vmo_read(blob_->GetVmo(), data, merkle_bytes + off, len);
    if (status == ZX_OK) {
        *actual = len;
    }
    return status;
}

void VnodeBlob::QueueUnlink() {
    flags_ |= kBlobFlagDeletable;
    // Attempt to purge in case the blob has been unlinked with no open fds
    TryPurge();
}

zx_status_t VnodeBlob::VerifyBlob(Blobfs* bs, size_t node_index) {
    blobfs_inode_t* inode = bs->GetNode(node_index);
    Digest digest(inode->merkle_root_hash);
    fbl::AllocChecker ac;
    fbl::RefPtr<VnodeBlob> vn =
        fbl::AdoptRef(new (&ac) VnodeBlob(bs, digest));

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    vn->PopulateInode(node_index);
    vn->InitVmos();

    // Set blob state to "Purged" so we do not try to add it to the cached map on recycle.
    vn->SetState(kBlobStatePurged);
    return vn->Verify();
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
        size_t hint = block_map_.size() - fbl::min(num_blocks, block_map_.size());
        if (AddBlocks(num_blocks) != ZX_OK) {
            return ZX_ERR_NO_SPACE;
        } else if ((status = FindBlocks(hint, num_blocks, block_index_out)) != ZX_OK) {
            return ZX_ERR_NO_SPACE;
        }
    }

    status = reserved_blocks_.Set(*block_index_out, *block_index_out + num_blocks);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    return ZX_OK;
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

zx_status_t Blobfs::FindNode(size_t start, size_t end, size_t* node_index_out) {
    for (size_t i = start; i < end; ++i) {
        if (GetNode(i)->start_block == kStartBlockFree) {
            // Found a free node. Mark it as reserved so no one else can allocate it.
            if (!reserved_nodes_.Get(i, i + 1, nullptr)) {
                reserved_nodes_.Set(i, i + 1);
                *node_index_out = i;
                return ZX_OK;
            }
        }
    }

    return ZX_ERR_OUT_OF_RANGE;
}

// Reserves a node IN MEMORY.
zx_status_t Blobfs::ReserveNode(size_t* node_index_out) {
    TRACE_DURATION("blobfs", "Blobfs::ReserveNode");
    zx_status_t status;
    if ((status = FindNode(0, info_.inode_count, node_index_out)) == ZX_OK) {
        return ZX_OK;
    }

    // If we didn't find any free inodes, try adding more via FVM.
    size_t old_inode_count = info_.inode_count;
    if (AddInodes() != ZX_OK) {
        return ZX_ERR_NO_SPACE;
    }

    if ((status = FindNode(old_inode_count, info_.inode_count, node_index_out)) == ZX_OK) {
        return ZX_OK;
    }

    return ZX_ERR_NO_SPACE;
}

void Blobfs::PersistNode(WritebackWork* wb, size_t node_index, const blobfs_inode_t& inode) {
    TRACE_DURATION("blobfs", "Blobfs::AllocateNode");

    ZX_DEBUG_ASSERT(inode.start_block >= kStartBlockMinimum);
    blobfs_inode_t* mapped_inode = GetNode(node_index);
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
    blobfs_inode_t* mapped_inode = GetNode(node_index);

    // Write to disk if node has been allocated within inode table
    if (mapped_inode->start_block >= kStartBlockMinimum) {
        ZX_DEBUG_ASSERT(wb != nullptr);
        *mapped_inode = {};
        info_.alloc_inode_count--;
        WriteNode(wb, node_index);
        WriteInfo(wb);
    }

    zx_status_t status = reserved_nodes_.Clear(node_index, node_index + 1);
    ZX_DEBUG_ASSERT(status == ZX_OK);
}

zx_status_t Blobfs::InitializeWriteback() {
    zx_status_t status;
    fbl::unique_ptr<fs::MappedVmo> buffer;
    constexpr size_t kWriteBufferSize = 64 * (1LU << 20);
    static_assert(kWriteBufferSize % kBlobfsBlockSize == 0,
                  "Buffer Size must be a multiple of the Blobfs Block Size");
    if ((status = fs::MappedVmo::Create(kWriteBufferSize, "blobfs-writeback",
                                        &buffer)) != ZX_OK) {
        return status;
    }
    if ((status = WritebackBuffer::Create(this, fbl::move(buffer), &writeback_)) != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

void Blobfs::Shutdown(fs::Vfs::ShutdownCallback cb) {
    TRACE_DURATION("blobfs", "Blobfs::Unmount");
    ZX_DEBUG_ASSERT_MSG(writeback_ != nullptr, "Shutdown requires writeback thread to sync");

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
            async::PostTask(async(), [this, cb = fbl::move(cb)]() mutable {
                // 3) Ensure the underlying disk has also flushed.
                fsync(blockfd_.get());

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
    uint64_t b = (map_index * sizeof(blobfs_inode_t)) / kBlobfsBlockSize;
    wb->Enqueue(node_map_->GetVmo(), b, NodeMapStartBlock(info_) + b, 1);
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
        EnqueueWork(fbl::move(wb));
        return ZX_OK;
    }
    default: {
        assert(false);
    }
    }
    return ZX_ERR_NOT_SUPPORTED;
}

void Blobfs::WriteInfo(WritebackWork* wb) {
    void* infodata = info_vmo_->GetData();
    memcpy(infodata, &info_, sizeof(info_));
    wb->Enqueue(info_vmo_->GetVmo(), 0, 0, 1);
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
            if ((r = df.Next(fbl::StringPiece(name, Digest::kLength * 2),
                             VTYPE_TO_DTYPE(V_TYPE_FILE))) != ZX_OK) {
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
            vn = fbl::internal::MakeRefPtrUpgradeFromRaw(raw_vn, hash_lock_);
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
    request.txnid = TxnId();
    request.vmoid = vmoid;
    request.opcode = BLOCKIO_CLOSE_VMO;
    return Txn(&request, 1);
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

    if (node_map_->Grow(inoblks * kBlobfsBlockSize) != ZX_OK) {
        return ZX_ERR_NO_SPACE;
    }

    info_.vslice_count += request.length;
    info_.ino_slices += static_cast<uint32_t>(request.length);
    info_.inode_count = inodes;

    // Reset new inodes to 0
    uintptr_t addr = reinterpret_cast<uintptr_t>(node_map_->GetData());
    memset(reinterpret_cast<void*>(addr + kBlobfsBlockSize * inoblks_old), 0,
           (kBlobfsBlockSize * (inoblks - inoblks_old)));

    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = CreateWork(&wb, nullptr)) != ZX_OK) {
        return status;
    }

    WriteInfo(wb.get());
    wb.get()->Enqueue(node_map_->GetVmo(), inoblks_old, NodeMapStartBlock(info_) + inoblks_old,
                inoblks - inoblks_old);
    EnqueueWork(fbl::move(wb));
    return ZX_OK;
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
    uint64_t abmblks_old = (info_.block_count + kBlobfsBlockBits - 1) / kBlobfsBlockBits;
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
    info_.block_count = blocks;

    WriteInfo(wb.get());
    EnqueueWork(fbl::move(wb));
    return ZX_OK;
}

void Blobfs::Sync(SyncCallback closure) {
    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = CreateWork(&wb, nullptr)) != ZX_OK) {
        closure(status);
        return;
    }

    wb->SetClosure(fbl::move(closure));
    EnqueueWork(fbl::move(wb));
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

Blobfs::Blobfs(fbl::unique_fd fd, const blobfs_info_t* info)
    : blockfd_(fbl::move(fd)) {
    memcpy(&info_, info, sizeof(blobfs_info_t));
}

Blobfs::~Blobfs() {
    writeback_ = nullptr;

    ZX_ASSERT(open_hash_.is_empty());
    closed_hash_.clear();

    if (fifo_client_ != nullptr) {
        FreeTxnId();
        ioctl_block_fifo_close(Fd());
        block_fifo_release_client(fifo_client_);
    }
}

zx_status_t Blobfs::Create(fbl::unique_fd fd, const blobfs_info_t* info,
                           fbl::unique_ptr<Blobfs>* out) {
    TRACE_DURATION("blobfs", "Blobfs::Create");
    zx_status_t status = blobfs_check_info(info, TotalBlocks(*info));
    if (status < 0) {
        fprintf(stderr, "blobfs: Check info failure\n");
        return status;
    }

    fbl::AllocChecker ac;
    auto fs = fbl::unique_ptr<Blobfs>(new Blobfs(fbl::move(fd), info));

    zx_handle_t fifo;
    ssize_t r;
    if ((r = ioctl_block_get_info(fs->Fd(), &fs->block_info_)) < 0) {
        return static_cast<zx_status_t>(r);
    } else if (kBlobfsBlockSize % fs->block_info_.block_size != 0) {
        return ZX_ERR_IO;
    } else if ((r = ioctl_block_get_fifos(fs->Fd(), &fifo)) < 0) {
        fprintf(stderr, "Failed to mount blobfs: Someone else is using the block device\n");
        return static_cast<zx_status_t>(r);
    } else if (fs->TxnId() == TXNID_INVALID) {
        zx_handle_close(fifo);
        return static_cast<zx_status_t>(ZX_ERR_NO_RESOURCES);
    } else if ((status = block_fifo_create_client(fifo, &fs->fifo_client_)) != ZX_OK) {
        fs->FreeTxnId();
        zx_handle_close(fifo);
        return status;
    }

    // Keep the block_map_ aligned to a block multiple
    if ((status = fs->block_map_.Reset(BlockMapBlocks(fs->info_) * kBlobfsBlockBits)) < 0) {
        fprintf(stderr, "blobfs: Could not reset block bitmap\n");
        return status;
    } else if ((status = fs->block_map_.Shrink(fs->info_.block_count)) < 0) {
        fprintf(stderr, "blobfs: Could not shrink block bitmap\n");
        return status;
    }

    size_t nodemap_size = kBlobfsInodeSize * fs->info_.inode_count;
    ZX_DEBUG_ASSERT(fbl::round_up(nodemap_size, kBlobfsBlockSize) == nodemap_size);
    ZX_DEBUG_ASSERT(nodemap_size / kBlobfsBlockSize == NodeMapBlocks(fs->info_));
    if ((status = fs::MappedVmo::Create(nodemap_size, "nodemap", &fs->node_map_)) != ZX_OK) {
        return status;
    } else if ((status = fs->AttachVmo(fs->block_map_.StorageUnsafe()->GetVmo(),
                                       &fs->block_map_vmoid_)) != ZX_OK) {
        return status;
    } else if ((status = fs->AttachVmo(fs->node_map_->GetVmo(),
                                       &fs->node_map_vmoid_)) != ZX_OK) {
        return status;
    } else if ((status = fs->LoadBitmaps()) < 0) {
        fprintf(stderr, "blobfs: Failed to load bitmaps: %d\n", status);
        return status;
    } else if ((status = fs::MappedVmo::Create(kBlobfsBlockSize, "blobfs-superblock",
                                               &fs->info_vmo_)) != ZX_OK) {
        fprintf(stderr, "blobfs: Failed to create info vmo: %d\n", status);
        return status;
    } else if ((status = fs->AttachVmo(fs->info_vmo_->GetVmo(),
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

    *out = fbl::move(fs);
    return ZX_OK;
}

zx_status_t Blobfs::InitializeVnodes() {
    fbl::AutoLock lock(&hash_lock_);
    for (size_t i = 0; i < info_.inode_count; ++i) {
        const blobfs_inode_t* inode = GetNode(i);
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

zx_status_t Blobfs::VnodeInsertClosedLocked(fbl::RefPtr<VnodeBlob> vn) {
    // To exist in the closed_hash_, this RefPtr must be leaked.
    if (!closed_hash_.insert_or_find(vn.get())) {
        // Set blob state to "Purged" so we do not try to add it to the cached map on recycle.
        vn->SetState(kBlobStatePurged);
        return ZX_ERR_ALREADY_EXISTS;
    }
    vn->TearDown();
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
    ReadTxn txn(this);
    txn.Enqueue(block_map_vmoid_, 0, BlockMapStartBlock(info_), BlockMapBlocks(info_));
    txn.Enqueue(node_map_vmoid_, 0, NodeMapStartBlock(info_), NodeMapBlocks(info_));
    return txn.Flush();
}

zx_status_t blobfs_create(fbl::unique_ptr<Blobfs>* out, fbl::unique_fd blockfd) {
    zx_status_t status;

    char block[kBlobfsBlockSize];
    if ((status = readblk(blockfd.get(), 0, (void*)block)) < 0) {
        fprintf(stderr, "blobfs: could not read info block\n");
        return status;
    }

    blobfs_info_t* info = reinterpret_cast<blobfs_info_t*>(&block[0]);

    uint64_t blocks;
    if ((status = blobfs_get_blockcount(blockfd.get(), &blocks)) != ZX_OK) {
        fprintf(stderr, "blobfs: cannot find end of underlying device\n");
        return status;
    }

    if ((status = blobfs_check_info(info, blocks)) != ZX_OK) {
        fprintf(stderr, "blobfs: Info check failed\n");
        return status;
    }

    if ((status = CheckFvmConsistency(info, blockfd.get())) != ZX_OK) {
        fprintf(stderr, "blobfs: FVM info check failed\n");
        return status;
    }

    if ((status = Blobfs::Create(fbl::move(blockfd), info, out)) != ZX_OK) {
        fprintf(stderr, "blobfs: mount failed; could not create blobfs\n");
        return status;
    }

    return ZX_OK;
}

zx_status_t blobfs_mount(async_t* async, fbl::unique_fd blockfd,
                         const blob_options_t* options, zx::channel root,
                         fbl::Closure on_unmount) {
    zx_status_t status;
    fbl::unique_ptr<Blobfs> fs;

    if ((status = blobfs_create(&fs, fbl::move(blockfd))) != ZX_OK) {
        return status;
    }

    if ((status = fs->InitializeWriteback()) != ZX_OK) {
        return status;
    }

    fs->SetAsync(async);
    fs->SetReadonly(options->readonly);
    if (options->metrics) {
        fs->CollectMetrics();
    }
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
} // namespace blobfs
