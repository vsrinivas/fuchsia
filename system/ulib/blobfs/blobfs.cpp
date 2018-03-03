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
#include <fbl/limits.h>
#include <fbl/ref_ptr.h>
#include <fdio/debug.h>
#include <fs/block-txn.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zx/event.h>

#define ZXDEBUG 0

#include <blobfs/blobfs.h>

using digest::Digest;
using digest::MerkleTree;

namespace blobfs {
namespace {

zx_status_t vmo_read_exact(zx_handle_t h, void* data, uint64_t offset, size_t len) {
    size_t actual;
    zx_status_t status = zx_vmo_read_old(h, data, offset, len, &actual);
    if (status != ZX_OK) {
        return status;
    } else if (actual != len) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t vmo_write_exact(zx_handle_t h, const void* data, uint64_t offset, size_t len) {
    size_t actual;
    zx_status_t status = zx_vmo_write_old(h, data, offset, len, &actual);
    if (status != ZX_OK) {
        return status;
    } else if (actual != len) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

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
        FS_TRACE_ERROR("blobfs: Missing slize\n");
        return ZX_ERR_BAD_STATE;
    }

    for (size_t i = 0; i < request.count; i++) {
        size_t actual_count = response.vslice_range[i].count;
        if (!response.vslice_range[i].allocated || expected_count[i] != actual_count) {
            // TODO(rvargas): Consider modifying the size automatically.
            FS_TRACE_ERROR("blobfs: Wrong slice size\n");
            return ZX_ERR_IO_DATA_INTEGRITY;
        }
    }

    return ZX_OK;
}

} // namespace

blobfs_inode_t* Blobfs::GetNode(size_t index) const {
    return &reinterpret_cast<blobfs_inode_t*>(node_map_->GetData())[index];
}

zx_status_t VnodeBlob::Verify() const {
    TRACE_DURATION("blobfs", "Blobfs::Verify");
    Duration duration(blobfs_->CollectingMetrics());

    const blobfs_inode_t* inode = blobfs_->GetNode(map_index_);
    const void* data = inode->blob_size ? GetData() : nullptr;
    const void* tree = inode->blob_size ? GetMerkle() : nullptr;
    const uint64_t data_size = inode->blob_size;
    const uint64_t merkle_size = MerkleTree::GetTreeLength(data_size);
    // TODO(smklein): We could lazily verify more of the VMO if
    // we could fault in pages on-demand.
    //
    // For now, we aggressively verify the entire VMO up front.
    Digest digest;
    digest = reinterpret_cast<const uint8_t*>(&digest_[0]);
    zx_status_t status = MerkleTree::Verify(data, data_size, tree,
                                            merkle_size, 0, data_size, digest);
    blobfs_->UpdateMerkleVerifyMetrics(data_size, merkle_size, duration.ns());
    return status;
}

zx_status_t VnodeBlob::InitVmos() {
    TRACE_DURATION("blobfs", "Blobfs::InitVmos");

    if (blob_ != nullptr) {
        return ZX_OK;
    }

    zx_status_t status;
    const blobfs_inode_t* inode = blobfs_->GetNode(map_index_);

    uint64_t num_blocks = BlobDataBlocks(*inode) + MerkleTreeBlocks(*inode);
    if ((status = MappedVmo::Create(num_blocks * kBlobfsBlockSize, "blob", &blob_)) != ZX_OK) {
        FS_TRACE_ERROR("Failed to initialize vmo; error: %d\n", status);
        BlobCloseHandles();
        return status;
    }
    if ((status = blobfs_->AttachVmo(blob_->GetVmo(), &vmoid_)) != ZX_OK) {
        FS_TRACE_ERROR("Failed to attach VMO to block device; error: %d\n", status);
        BlobCloseHandles();
        return status;
    }

    ReadTxn txn(blobfs_.get());
    Duration duration(blobfs_->CollectingMetrics());
    uint64_t start = inode->start_block + DataStartBlock(blobfs_->info_);
    uint64_t length = BlobDataBlocks(*inode) + MerkleTreeBlocks(*inode);
    txn.Enqueue(vmoid_, 0, start, length);
    if ((status = txn.Flush()) != ZX_OK) {
        return status;
    }
    uint64_t read_time = duration.ns();
    duration.reset();
    if ((status = Verify()) != ZX_OK) {
        return status;
    }
    blobfs_->UpdateMerkleDiskReadMetrics(length * kBlobfsBlockSize, read_time, duration.ns());
    return ZX_OK;
}

uint64_t VnodeBlob::SizeData() const {
    if (GetState() == kBlobStateReadable) {
        auto inode = blobfs_->GetNode(map_index_);
        return inode->blob_size;
    }
    return 0;
}

VnodeBlob::VnodeBlob(fbl::RefPtr<Blobfs> bs, const Digest& digest)
    : blobfs_(fbl::move(bs)),
      flags_(kBlobStateEmpty), syncing_(false), clone_watcher_(this) {
    digest.CopyTo(digest_, sizeof(digest_));
}

VnodeBlob::VnodeBlob(fbl::RefPtr<Blobfs> bs)
    : blobfs_(fbl::move(bs)),
      flags_(kBlobStateEmpty | kBlobFlagDirectory),
      syncing_(false), clone_watcher_(this) {}

void VnodeBlob::BlobCloseHandles() {
    blob_ = nullptr;
    readable_event_.reset();
}

zx_status_t VnodeBlob::SpaceAllocate(uint64_t size_data) {
    TRACE_DURATION("blobfs", "Blobfs::SpaceAllocate", "size_data", size_data);
    Duration duration(blobfs_->CollectingMetrics());

    if (GetState() != kBlobStateEmpty) {
        return ZX_ERR_BAD_STATE;
    }

    // Find a free node, mark it as reserved.
    zx_status_t status;
    if ((status = blobfs_->AllocateNode(&map_index_)) != ZX_OK) {
        return status;
    }

    // Initialize the inode with known fields
    blobfs_inode_t* inode = blobfs_->GetNode(map_index_);
    memset(inode->merkle_root_hash, 0, Digest::kLength);
    inode->blob_size = size_data;
    inode->num_blocks = MerkleTreeBlocks(*inode) + BlobDataBlocks(*inode);

    // Special case for the null blob: We skip the write phase
    if (inode->blob_size == 0) {
        // Toss a valid block to the null blob, to distinguish it from
        // unallocated nodes.
        inode->start_block = kStartBlockMinimum;
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
    if ((status = MappedVmo::Create(inode->num_blocks * kBlobfsBlockSize, "blob", &blob_)) != ZX_OK) {
        goto fail;
    }
    if ((status = blobfs_->AttachVmo(blob_->GetVmo(), &vmoid_)) != ZX_OK) {
        goto fail;
    }

    // Allocate space for the blob
    if ((status = blobfs_->AllocateBlocks(inode->num_blocks, &inode->start_block)) != ZX_OK) {
        goto fail;
    }

    SetState(kBlobStateDataWrite);
    blobfs_->UpdateAllocationMetrics(size_data, duration.ns());
    return ZX_OK;

fail:
    BlobCloseHandles();
    blobfs_->FreeNode(map_index_);
    return status;
}

// A helper function for dumping either the Merkle Tree or the actual blob data
// to both (1) The containing VMO, and (2) disk.
void VnodeBlob::WriteShared(WriteTxn* txn, size_t start, size_t len, uint64_t start_block) {
    TRACE_DURATION("blobfs", "Blobfs::WriteShared", "txn", txn, "start", start, "len", len,
                   "start_block", start_block);
    // Write as many 'entire blocks' as possible
    uint64_t n = start / kBlobfsBlockSize;
    uint64_t n_end = (start + len + kBlobfsBlockSize - 1) / kBlobfsBlockSize;
    txn->Enqueue(blob_->GetVmo(), n, n + start_block + DataStartBlock(blobfs_->info_), n_end - n);
}

void* VnodeBlob::GetData() const {
    auto inode = blobfs_->GetNode(map_index_);
    return fs::GetBlock<kBlobfsBlockSize>(blob_->GetData(),
                                        MerkleTreeBlocks(*inode));
}

void* VnodeBlob::GetMerkle() const {
    return blob_->GetData();
}

zx_status_t VnodeBlob::WriteMetadata(fbl::unique_ptr<WritebackWork> wb) {
    TRACE_DURATION("blobfs", "Blobfs::WriteMetadata");
    assert(GetState() == kBlobStateDataWrite);

    // All data has been written to the containing VMO
    SetState(kBlobStateReadable);
    if (readable_event_.is_valid()) {
        zx_status_t status = readable_event_.signal(0u, ZX_USER_SIGNAL_0);
        if (status != ZX_OK) {
            SetState(kBlobStateError);
            return status;
        }
    }

    atomic_store(&syncing_, true);
    auto inode = blobfs_->GetNode(map_index_);

    // Write block allocation bitmap
    blobfs_->WriteBitmap(wb->txn(), inode->num_blocks, inode->start_block);

    // Update the on-disk hash
    memcpy(inode->merkle_root_hash, &digest_[0], Digest::kLength);

    // Write back the blob node
    blobfs_->WriteNode(wb->txn(), map_index_);
    blobfs_->WriteInfo(wb->txn());
    wb->SetSyncComplete();
    blobfs_->EnqueueWork(fbl::move(wb));
    return ZX_OK;
}

zx_status_t VnodeBlob::WriteInternal(const void* data, size_t len, size_t* actual) {
    TRACE_DURATION("blobfs", "Blobfs::WriteInternal", "data", data, "len", len);

    *actual = 0;
    if (len == 0) {
        return ZX_OK;
    }

    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = blobfs_->CreateWork(&wb, this)) != ZX_OK) {
        return status;
    }

    auto inode = blobfs_->GetNode(map_index_);
    const size_t data_start = MerkleTreeBlocks(*inode) * kBlobfsBlockSize;
    if (GetState() == kBlobStateDataWrite) {
        size_t to_write = fbl::min(len, inode->blob_size - bytes_written_);
        size_t offset = bytes_written_ + data_start;
        zx_status_t status = vmo_write_exact(blob_->GetVmo(), data, offset, to_write);
        if (status != ZX_OK) {
            return status;
        }

        WriteShared(wb->txn(), offset, len, inode->start_block);

        *actual = to_write;
        bytes_written_ += to_write;

        // More data to write.
        if (bytes_written_ < inode->blob_size) {
            Duration duration(blobfs_->CollectingMetrics()); // Tracking enqueue time.
            blobfs_->EnqueueWork(fbl::move(wb));
            blobfs_->UpdateClientWriteMetrics(to_write, 0, duration.ns(), 0);
            return ZX_OK;
        }

        // TODO(smklein): As an optimization, use the CreateInit/Update/Final
        // methods to create the merkle tree as we write data, rather than
        // waiting until the data is fully downloaded to create the tree.
        size_t merkle_size = MerkleTree::GetTreeLength(inode->blob_size);
        uint64_t generation_time = 0;
        if (merkle_size > 0) {
            Digest digest;
            void* merkle_data = GetMerkle();
            const void* blob_data = GetData();
            Duration duration(blobfs_->CollectingMetrics()); // Tracking generation time.

            if ((status = MerkleTree::Create(blob_data, inode->blob_size, merkle_data,
                                             merkle_size, &digest)) != ZX_OK) {
                SetState(kBlobStateError);
                return status;
            } else if (digest != digest_) {
                // Downloaded blob did not match provided digest
                SetState(kBlobStateError);
                return ZX_ERR_IO_DATA_INTEGRITY;
            }

            WriteShared(wb->txn(), 0, merkle_size, inode->start_block);
            generation_time = duration.ns();
        } else if ((status = Verify()) != ZX_OK) {
            // Small blobs may not have associated Merkle Trees, and will
            // require validation, since we are not regenerating and checking
            // the digest.
            SetState(kBlobStateError);
            return status;
        }

        // No more data to write. Flush to disk.
        Duration duration(blobfs_->CollectingMetrics()); // Tracking enqueue time.
        if ((status = WriteMetadata(fbl::move(wb))) != ZX_OK) {
            SetState(kBlobStateError);
            return status;
        }

        blobfs_->UpdateClientWriteMetrics(to_write, merkle_size, duration.ns(),
                                          generation_time);
        return ZX_OK;
    }

    return ZX_ERR_BAD_STATE;
}

zx_status_t VnodeBlob::GetReadableEvent(zx_handle_t* out) {
    TRACE_DURATION("blobfs", "Blobfs::GetReadableEvent");
    zx_status_t status;
    // This is the first 'wait until read event' request received
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
    auto inode = blobfs_->GetNode(map_index_);
    if (inode->blob_size == 0) {
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = InitVmos();
    if (status != ZX_OK) {
        return status;
    }

    // TODO(smklein): Only clone / verify the part of the vmo that
    // was requested.
    const size_t data_start = MerkleTreeBlocks(*inode) * kBlobfsBlockSize;
    zx_handle_t clone;
    if ((status = zx_vmo_clone(blob_->GetVmo(), ZX_VMO_CLONE_COPY_ON_WRITE,
                               data_start, inode->blob_size, &clone)) != ZX_OK) {
        return status;
    }

    if ((status = zx_handle_replace(clone, rights, out)) != ZX_OK) {
        zx_handle_close(clone);
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
        clone_watcher_.Begin(blobfs_->GetAsync());
    }

    return ZX_OK;
}

async_wait_result_t VnodeBlob::HandleNoClones(async_t* async, zx_status_t status,
                                              const zx_packet_signal_t* signal) {
    ZX_DEBUG_ASSERT(status == ZX_OK);
    ZX_DEBUG_ASSERT((signal->observed & ZX_VMO_ZERO_CHILDREN) != 0);
    clone_watcher_.set_object(ZX_HANDLE_INVALID);
    clone_ref_ = nullptr;
    return ASYNC_WAIT_FINISHED;
}

zx_status_t VnodeBlob::ReadInternal(void* data, size_t len, size_t off, size_t* actual) {
    TRACE_DURATION("blobfs", "Blobfs::ReadInternal", "len", len, "off", off);

    if (GetState() != kBlobStateReadable) {
        return ZX_ERR_BAD_STATE;
    }

    auto inode = blobfs_->GetNode(map_index_);
    if (inode->blob_size == 0) {
        *actual = 0;
        return ZX_OK;
    }

    zx_status_t status = InitVmos();
    if (status != ZX_OK) {
        return status;
    }

    Digest d;
    d = reinterpret_cast<const uint8_t*>(&digest_[0]);
    if (off >= inode->blob_size) {
        *actual = 0;
        return ZX_OK;
    }
    if (len > (inode->blob_size - off)) {
        len = inode->blob_size - off;
    }

    const size_t data_start = MerkleTreeBlocks(*inode) * kBlobfsBlockSize;
    return zx_vmo_read_old(blob_->GetVmo(), data, data_start + off, len, actual);
}

void VnodeBlob::QueueUnlink() {
    flags_ |= kBlobFlagDeletable;
    // Attempt to purge in case the blob has been unlinked with no open fds
    TryPurge();
}

// Allocates Blocks IN MEMORY
zx_status_t Blobfs::AllocateBlocks(size_t nblocks, size_t* blkno_out) {
    TRACE_DURATION("blobfs", "Blobfs::AllocateBlocks", "nblocks", nblocks);

    zx_status_t status;
    if ((status = block_map_.Find(false, 0, block_map_.size(), nblocks, blkno_out)) != ZX_OK) {
        // If we have run out of blocks, attempt to add block slices via FVM
        size_t old_size = block_map_.size();
        if (AddBlocks(nblocks) != ZX_OK) {
            return ZX_ERR_NO_SPACE;
        } else if (block_map_.Find(false, old_size, block_map_.size(), nblocks, blkno_out) != ZX_OK) {
            return ZX_ERR_NO_SPACE;
        }
    }
    status = block_map_.Set(*blkno_out, *blkno_out + nblocks);
    assert(status == ZX_OK);
    info_.alloc_block_count += nblocks;
    return ZX_OK;
}

// Frees Blocks IN MEMORY
void Blobfs::FreeBlocks(size_t nblocks, size_t blkno) {
    TRACE_DURATION("blobfs", "Blobfs::FreeBlocks", "nblocks", nblocks, "blkno", blkno);
    zx_status_t status = block_map_.Clear(blkno, blkno + nblocks);
    info_.alloc_block_count -= nblocks;
    assert(status == ZX_OK);
}

// Allocates a node IN MEMORY
zx_status_t Blobfs::AllocateNode(size_t* node_index_out) {
    TRACE_DURATION("blobfs", "Blobfs::AllocateNode");
    for (size_t i = 0; i < info_.inode_count; ++i) {
        if (GetNode(i)->start_block == kStartBlockFree) {
            // Found a free node. Mark it as reserved so no one else can allocate it.
            GetNode(i)->start_block = kStartBlockReserved;
            info_.alloc_inode_count++;
            *node_index_out = i;
            return ZX_OK;
        }
    }

    // If we didn't find any free inodes, try adding more via FVM.
    size_t old_inode_count = info_.inode_count;
    if (AddInodes() != ZX_OK) {
        return ZX_ERR_NO_SPACE;
    }

    for (size_t i = old_inode_count; i < info_.inode_count; ++i) {
        if (GetNode(i)->start_block == kStartBlockFree) {
            // Found a free node. Mark it as reserved so no one else can allocate it.
            GetNode(i)->start_block = kStartBlockReserved;
            info_.alloc_inode_count++;
            *node_index_out = i;
            return ZX_OK;
        }
    }

    return ZX_ERR_NO_SPACE;
}

// Frees a node IN MEMORY
void Blobfs::FreeNode(size_t node_index) {
    TRACE_DURATION("blobfs", "Blobfs::FreeNode", "node_index", node_index);
    memset(GetNode(node_index), 0, sizeof(blobfs_inode_t));
    info_.alloc_inode_count--;
}

//TODO(planders): Make sure all client-side connections are properly destroyed before shutdown
zx_status_t Blobfs::Unmount() {
    TRACE_DURATION("blobfs", "Blobfs::Unmount");

    // Ensure writeback buffer completes before auxilliary structures
    // are deleted.
    fsync(blockfd_.get());

    // Explicitly delete this (rather than just letting the memory release when
    // the process exits) to ensure that the block device's fifo has been
    // closed.
    delete this;

    // TODO(smklein): To not bind filesystem lifecycle to a process, shut
    // down (closing dispatcher) rather than calling exit.
    exit(0);
    return ZX_OK;
}

void Blobfs::WriteBitmap(WriteTxn* txn, uint64_t nblocks, uint64_t start_block) {
    TRACE_DURATION("blobfs", "Blobfs::WriteBitmap", "nblocks", nblocks, "start_block",
                   start_block);
    uint64_t bbm_start_block = start_block / kBlobfsBlockBits;
    uint64_t bbm_end_block = fbl::round_up(start_block + nblocks,
                                           kBlobfsBlockBits) /
                             kBlobfsBlockBits;

    // Write back the block allocation bitmap
    txn->Enqueue(block_map_.StorageUnsafe()->GetVmo(), bbm_start_block,
                 BlockMapStartBlock(info_) + bbm_start_block, bbm_end_block - bbm_start_block);
}

void Blobfs::WriteNode(WriteTxn* txn, size_t map_index) {
    TRACE_DURATION("blobfs", "Blobfs::WriteNode", "map_index", map_index);
    uint64_t b = (map_index * sizeof(blobfs_inode_t)) / kBlobfsBlockSize;
    txn->Enqueue(node_map_->GetVmo(), b, NodeMapStartBlock(info_) + b, 1);
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
    *out = fbl::AdoptRef(new (&ac) VnodeBlob(fbl::RefPtr<Blobfs>(this), digest));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    VnodeInsert(out->get());
    return ZX_OK;
}

// If no client references to the blob still exist and the blob is either queued for deletion or
// not in a readable state, purge all traces of the blob from blobfs.
// This is only called when we do not expect the blob to be accessed again.
zx_status_t Blobfs::PurgeBlob(VnodeBlob* vn) {
    TRACE_DURATION("blobfs", "Blobfs::PurgeBlob");

    switch (vn->GetState()) {
    case kBlobStateEmpty: {
        VnodeRelease(vn);
        return ZX_OK;
    }
    case kBlobStateReadable: {
        // A readable blob should only be purged if it has been unlinked
        ZX_ASSERT(vn->DeletionQueued());
        // Fall-through
    }
    case kBlobStateDataWrite:
    case kBlobStateError: {
        size_t node_index = vn->GetMapIndex();
        uint64_t start_block = GetNode(node_index)->start_block;
        uint64_t nblocks = GetNode(node_index)->num_blocks;
        FreeNode(node_index);
        FreeBlocks(nblocks, start_block);
        zx_status_t status;
        fbl::unique_ptr<WritebackWork> wb;
        if ((status = CreateWork(&wb, vn)) != ZX_OK) {
            return status;
        }
        WriteTxn* txn = wb->txn();
        WriteNode(txn, node_index);
        WriteBitmap(txn, nblocks, start_block);
        WriteInfo(txn);
        VnodeRelease(vn);
        EnqueueWork(fbl::move(wb));
        return ZX_OK;
    }
    default: {
        assert(false);
    }
    }
    return ZX_ERR_NOT_SUPPORTED;
}

void Blobfs::WriteInfo(WriteTxn* txn) {
    void* infodata = info_vmo_->GetData();
    memcpy(infodata, &info_, sizeof(info_));
    txn->Enqueue(info_vmo_->GetVmo(), 0, 0, 1);
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
    fbl::RefPtr<VnodeBlob> vn;
    // Look up blob in the fast map (is the blob open elsewhere?)
    {
        // Avoid releasing a reference to |vn| while holding |hash_lock_|.
        fbl::AutoLock lock(&hash_lock_);
        auto raw_vn = hash_.find(digest.AcquireBytes()).CopyPointer();
        digest.ReleaseBytes();

        if (raw_vn != nullptr) {
            vn = fbl::internal::MakeRefPtrUpgradeFromRaw(raw_vn, hash_lock_);

            if (vn == nullptr) {
                // Blob was found but is being deleted - remove from hash so we don't collide
                VnodeReleaseLocked(raw_vn);
            }
        }
    }

    if (vn != nullptr) {
        if (out != nullptr) {
            UpdateLookupMetrics(vn->SizeData());
            *out = fbl::move(vn);
        }
        return ZX_OK;
    }

    // Look up blob in the slow map
    for (size_t i = 0; i < info_.inode_count; ++i) {
        if (GetNode(i)->start_block >= kStartBlockMinimum) {
            if (digest == GetNode(i)->merkle_root_hash) {
                if (out != nullptr) {
                    // Found it. Attempt to wrap the blob in a vnode.
                    fbl::AllocChecker ac;
                    vn = fbl::AdoptRef(new (&ac) VnodeBlob(fbl::RefPtr<Blobfs>(this), digest));
                    if (!ac.check()) {
                        return ZX_ERR_NO_MEMORY;
                    }
                    vn->SetState(kBlobStateReadable);
                    vn->SetMapIndex(i);
                    // Delay reading any data from disk until read.
                    VnodeInsert(vn.get());
                    UpdateLookupMetrics(vn->SizeData());
                    *out = fbl::move(vn);
                }
                return ZX_OK;
            }
        }
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

    WriteInfo(wb->txn());
    wb->txn()->Enqueue(node_map_->GetVmo(), inoblks_old, NodeMapStartBlock(info_) + inoblks_old,
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

    if (abmblks > abmblks_old) {
        wb->txn()->Enqueue(block_map_.StorageUnsafe()->GetVmo(), abmblks_old,
                     DataStartBlock(info_) + abmblks_old, abmblks - abmblks_old);
    }

    info_.vslice_count += request.length;
    info_.dat_slices += static_cast<uint32_t>(request.length);
    info_.block_count = blocks;

    WriteInfo(wb->txn());
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

void Blobfs::UpdateAllocationMetrics(uint64_t size_data, uint64_t ns) {
    if (CollectingMetrics()) {
        metrics_.blobs_created++;
        metrics_.blobs_created_total_size += size_data;
        metrics_.total_allocation_time_ns += ns;
    }
}

void Blobfs::UpdateLookupMetrics(uint64_t size) {
    if (CollectingMetrics()) {
        metrics_.blobs_opened++;
        metrics_.blobs_opened_total_size += size;
    }
}

void Blobfs::UpdateClientWriteMetrics(uint64_t data_size, uint64_t merkle_size,
                                      uint64_t enqueue_ns, uint64_t generate_ns) {
    if (CollectingMetrics()) {
        metrics_.data_bytes_written += data_size;
        metrics_.merkle_bytes_written += merkle_size;
        metrics_.total_write_enqueue_time_ns += enqueue_ns;
        metrics_.total_merkle_generation_time_ns += generate_ns;
    }
}

void Blobfs::UpdateWritebackMetrics(uint64_t size, uint64_t ns) {
    if (CollectingMetrics()) {
        metrics_.total_writeback_time_ns += ns;
        metrics_.total_writeback_bytes_written += size;
    }
}

void Blobfs::UpdateMerkleDiskReadMetrics(uint64_t size, uint64_t read_ns, uint64_t verify_ns) {
    if (CollectingMetrics()) {
        metrics_.total_read_from_disk_time_ns += read_ns;
        metrics_.total_read_from_disk_verify_time_ns += verify_ns;
        metrics_.bytes_read_from_disk += size;
    }
}

void Blobfs::UpdateMerkleVerifyMetrics(uint64_t size_data, uint64_t size_merkle, uint64_t ns) {
    if (CollectingMetrics()) {
        metrics_.blobs_verified++;
        metrics_.blobs_verified_total_size_data += size_data;
        metrics_.blobs_verified_total_size_merkle += size_merkle;
        metrics_.total_verification_time_ns += ns;
    }
}

Blobfs::Blobfs(fbl::unique_fd fd, const blobfs_info_t* info)
    : blockfd_(fbl::move(fd)) {
    memcpy(&info_, info, sizeof(blobfs_info_t));
}

Blobfs::~Blobfs() {
    writeback_ = nullptr;
    ZX_ASSERT(hash_.is_empty());

    if (fifo_client_ != nullptr) {
        FreeTxnId();
        ioctl_block_fifo_close(Fd());
        block_fifo_release_client(fifo_client_);
    }
}

zx_status_t Blobfs::Create(fbl::unique_fd fd, const blobfs_info_t* info,
                           fbl::RefPtr<Blobfs>* out) {
    TRACE_DURATION("blobfs", "Blobfs::Create");
    zx_status_t status = blobfs_check_info(info, TotalBlocks(*info));
    if (status < 0) {
        fprintf(stderr, "blobfs: Check info failure\n");
        return status;
    }

    fbl::AllocChecker ac;
    fbl::RefPtr<Blobfs> fs = fbl::AdoptRef(new (&ac) Blobfs(fbl::move(fd), info));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_handle_t fifo;
    ssize_t r;
    if ((r = ioctl_block_get_info(fs->Fd(), &fs->block_info_)) < 0) {
        return static_cast<zx_status_t>(r);
    } else if (kBlobfsBlockSize % fs->block_info_.block_size != 0) {
        return ZX_ERR_IO;
    } else if ((r = ioctl_block_get_fifos(fs->Fd(), &fifo)) < 0) {
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
    if ((status = MappedVmo::Create(nodemap_size, "nodemap", &fs->node_map_)) != ZX_OK) {
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
    } else if ((status = MappedVmo::Create(kBlobfsBlockSize, "blobfs-superblock",
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
    }

    fbl::unique_ptr<MappedVmo> buffer;
    constexpr size_t kWriteBufferSize = 64 * (1LU << 20);
    static_assert(kWriteBufferSize % kBlobfsBlockSize == 0,
                  "Buffer Size must be a multiple of the Blobfs Block Size");
    if ((status = MappedVmo::Create(kWriteBufferSize, "blobfs-writeback",
                                    &buffer)) != ZX_OK) {
        return status;
    }
    if ((status = WritebackBuffer::Create(fs.get(), fbl::move(buffer),
                                          &fs->writeback_)) != ZX_OK) {
        return status;
    }

    *out = fs;
    return ZX_OK;
}

zx_status_t Blobfs::GetRootBlob(fbl::RefPtr<VnodeBlob>* out) {
    fbl::AllocChecker ac;
    fbl::RefPtr<VnodeBlob> vn =
        fbl::AdoptRef(new (&ac) VnodeBlob(fbl::RefPtr<Blobfs>(this)));

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    *out = fbl::move(vn);

    return ZX_OK;
}

zx_status_t Blobfs::LoadBitmaps() {
    TRACE_DURATION("blobfs", "Blobfs::LoadBitmaps");
    ReadTxn txn(this);
    txn.Enqueue(block_map_vmoid_, 0, BlockMapStartBlock(info_), BlockMapBlocks(info_));
    txn.Enqueue(node_map_vmoid_, 0, NodeMapStartBlock(info_), NodeMapBlocks(info_));
    return txn.Flush();
}

zx_status_t blobfs_create(fbl::RefPtr<Blobfs>* out, fbl::unique_fd blockfd) {
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

zx_status_t blobfs_mount(async_t* async, fbl::unique_fd blockfd, bool metrics,
                         fbl::RefPtr<VnodeBlob>* out) {
    zx_status_t status;
    fbl::RefPtr<Blobfs> fs;

    if ((status = blobfs_create(&fs, fbl::move(blockfd))) != ZX_OK) {
        return status;
    }

    fs->SetAsync(async);
    if (metrics) {
        fs->CollectMetrics();
    }

    if ((status = fs->GetRootBlob(out)) != ZX_OK) {
        fprintf(stderr, "blobfs: mount failed; could not get root blob\n");
        return status;
    }

    return ZX_OK;
}
} // namespace blobfs
