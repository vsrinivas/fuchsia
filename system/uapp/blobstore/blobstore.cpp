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
#include <fs/block-txn.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <mxio/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/limits.h>
#include <fbl/ref_ptr.h>

#define MXDEBUG 0

#include "blobstore-private.h"

using digest::Digest;
using digest::MerkleTree;

namespace {

mx_status_t vmo_read_exact(mx_handle_t h, void* data, uint64_t offset, size_t len) {
    size_t actual;
    mx_status_t status = mx_vmo_read(h, data, offset, len, &actual);
    if (status != MX_OK) {
        return status;
    } else if (actual != len) {
        return MX_ERR_IO;
    }
    return MX_OK;
}

mx_status_t vmo_write_exact(mx_handle_t h, const void* data, uint64_t offset, size_t len) {
    size_t actual;
    mx_status_t status = mx_vmo_write(h, data, offset, len, &actual);
    if (status != MX_OK) {
        return status;
    } else if (actual != len) {
        return MX_ERR_IO;
    }
    return MX_OK;
}

} // namespace

namespace blobstore {

blobstore_inode_t* Blobstore::GetNode(size_t index) const {
    return &reinterpret_cast<blobstore_inode_t*>(node_map_->GetData())[index];
}

mx_status_t VnodeBlob::InitVmos() {
    if (blob_ != nullptr) {
        return MX_OK;
    }

    mx_status_t status;
    blobstore_inode_t* inode = blobstore_->GetNode(map_index_);

    uint64_t num_blocks = BlobDataBlocks(*inode) + MerkleTreeBlocks(*inode);
    if ((status = MappedVmo::Create(num_blocks * kBlobstoreBlockSize, "blob", &blob_)) != MX_OK) {
        FS_TRACE_ERROR("Failed to initialize vmo; error: %d\n", status);
        BlobCloseHandles();
        return status;
    }
    if ((status = blobstore_->AttachVmo(blob_->GetVmo(), &vmoid_)) != MX_OK) {
        FS_TRACE_ERROR("Failed to attach VMO to block device; error: %d\n", status);
        BlobCloseHandles();
        return status;
    }

    ReadTxn txn(blobstore_.get());
    txn.Enqueue(vmoid_, 0, inode->start_block, BlobDataBlocks(*inode) + MerkleTreeBlocks(*inode));
    return txn.Flush();
}

uint64_t VnodeBlob::SizeData() const {
    if (GetState() == kBlobStateReadable) {
        auto inode = blobstore_->GetNode(map_index_);
        return inode->blob_size;
    }
    return 0;
}

VnodeBlob::VnodeBlob(fbl::RefPtr<Blobstore> bs, const Digest& digest)
    : blobstore_(fbl::move(bs)),
      flags_(kBlobStateEmpty) {

    digest.CopyTo(digest_, sizeof(digest_));
}

VnodeBlob::VnodeBlob(fbl::RefPtr<Blobstore> bs)
    : blobstore_(fbl::move(bs)),
      flags_(kBlobStateEmpty | kBlobFlagDirectory) {}

void VnodeBlob::BlobCloseHandles() {
    blob_ = nullptr;
    readable_event_.reset();
}

mx_status_t VnodeBlob::SpaceAllocate(uint64_t size_data) {
    if (size_data == 0) {
        return MX_ERR_INVALID_ARGS;
    }
    if (GetState() != kBlobStateEmpty) {
        return MX_ERR_BAD_STATE;
    }

    // Find a free node, mark it as reserved.
    mx_status_t status;
    if ((status = blobstore_->AllocateNode(&map_index_)) != MX_OK) {
        return status;
    }

    // Initialize the inode with known fields
    blobstore_inode_t* inode = blobstore_->GetNode(map_index_);
    memset(inode->merkle_root_hash, 0, Digest::kLength);
    inode->blob_size = size_data;
    inode->num_blocks = MerkleTreeBlocks(*inode) + BlobDataBlocks(*inode);

    // Open VMOs, so we can begin writing after allocate succeeds.
    if ((status = MappedVmo::Create(inode->num_blocks * kBlobstoreBlockSize, "blob", &blob_)) != MX_OK) {
        goto fail;
    }
    if ((status = blobstore_->AttachVmo(blob_->GetVmo(), &vmoid_)) != MX_OK) {
        goto fail;
    }

    // Allocate space for the blob
    if ((status = blobstore_->AllocateBlocks(inode->num_blocks, &inode->start_block)) != MX_OK) {
        goto fail;
    }

    SetState(kBlobStateDataWrite);
    return MX_OK;

fail:
    BlobCloseHandles();
    blobstore_->FreeNode(map_index_);
    return status;
}

// A helper function for dumping either the Merkle Tree or the actual blob data
// to both (1) The containing VMO, and (2) disk.
mx_status_t VnodeBlob::WriteShared(WriteTxn* txn, size_t start, size_t len, uint64_t start_block) {
    // Write as many 'entire blocks' as possible
    uint64_t n = start / kBlobstoreBlockSize;
    uint64_t n_end = (start + len + kBlobstoreBlockSize - 1) / kBlobstoreBlockSize;
    txn->Enqueue(vmoid_, n, n + start_block, n_end - n);
    return txn->Flush();
}

void* VnodeBlob::GetData() const {
    auto inode = blobstore_->GetNode(map_index_);
    return fs::GetBlock<kBlobstoreBlockSize>(blob_->GetData(),
                                             MerkleTreeBlocks(*inode));
}

void* VnodeBlob::GetMerkle() const {
    return blob_->GetData();
}

mx_status_t VnodeBlob::WriteMetadata() {
    assert(GetState() == kBlobStateDataWrite);

    // All data has been written to the containing VMO
    SetState(kBlobStateReadable);
    if (readable_event_.is_valid()) {
        mx_status_t status = readable_event_.signal(0u, MX_USER_SIGNAL_0);
        if (status != MX_OK) {
            SetState(kBlobStateError);
            return status;
        }
    }

    // TODO(smklein): We could probably flush out these disk structures asynchronously.
    // Even writing the above blocks could be done async. The "node" write must be done
    // LAST, after everything else is complete, but that's the only restriction.
    //
    // This 'kBlobFlagSync' is currently not used, but it indicates when the sync is
    // complete.
    flags_ |= kBlobFlagSync;
    auto inode = blobstore_->GetNode(map_index_);

    WriteTxn txn(blobstore_.get());

    // Write block allocation bitmap
    if (blobstore_->WriteBitmap(&txn, inode->num_blocks, inode->start_block) != MX_OK) {
        return MX_ERR_IO;
    }

    // Flush the block allocation bitmap to disk
    fsync(blobstore_->blockfd_);

    // Update the on-disk hash
    memcpy(inode->merkle_root_hash, &digest_[0], Digest::kLength);

    // Write back the blob node
    if (blobstore_->WriteNode(&txn, map_index_)) {
        return MX_ERR_IO;
    }

    blobstore_->CountUpdate(&txn);
    flags_ &= ~kBlobFlagSync;
    return MX_OK;
}

mx_status_t VnodeBlob::WriteInternal(const void* data, size_t len, size_t* actual) {
    *actual = 0;
    if (len == 0) {
        return MX_OK;
    }

    WriteTxn txn(blobstore_.get());
    auto inode = blobstore_->GetNode(map_index_);
    const size_t data_start = MerkleTreeBlocks(*inode) * kBlobstoreBlockSize;
    if (GetState() == kBlobStateDataWrite) {
        size_t to_write = fbl::min(len, inode->blob_size - bytes_written_);
        size_t offset = bytes_written_ + data_start;
        mx_status_t status = vmo_write_exact(blob_->GetVmo(), data, offset, to_write);
        if (status != MX_OK) {
            return status;
        }

        status = WriteShared(&txn, offset, len, inode->start_block);
        if (status != MX_OK) {
            SetState(kBlobStateError);
            return status;
        }

        *actual = to_write;
        bytes_written_ += to_write;

        // More data to write.
        if (bytes_written_ < inode->blob_size) {
            return MX_OK;
        }

        // TODO(smklein): As an optimization, use the CreateInit/Update/Final
        // methods to create the merkle tree as we write data, rather than
        // waiting until the data is fully downloaded to create the tree.
        size_t merkle_size = MerkleTree::GetTreeLength(inode->blob_size);
        if (merkle_size > 0) {
            Digest digest;
            void* merkle_data = GetMerkle();
            const void* blob_data = GetData();
            if (MerkleTree::Create(blob_data, inode->blob_size, merkle_data,
                                   merkle_size, &digest) != MX_OK) {
                SetState(kBlobStateError);
                return status;
            } else if (digest != digest_) {
                // Downloaded blob did not match provided digest
                SetState(kBlobStateError);
                return status;
            }

            status = WriteShared(&txn, 0, merkle_size, inode->start_block);
            if (status != MX_OK) {
                SetState(kBlobStateError);
                return status;
            }
        }

        // No more data to write. Flush to disk.
        if ((status = WriteMetadata()) != MX_OK) {
            SetState(kBlobStateError);
            return status;
        }
        return MX_OK;
    }

    return MX_ERR_BAD_STATE;
}

mx_status_t VnodeBlob::GetReadableEvent(mx_handle_t* out) {
    mx_status_t status;
    // This is the first 'wait until read event' request received
    if (!readable_event_.is_valid()) {
        status = mx::event::create(0, &readable_event_);
        if (status != MX_OK) {
            return status;
        } else if (GetState() == kBlobStateReadable) {
            readable_event_.signal(0u, MX_USER_SIGNAL_0);
        }
    }
    status = mx_handle_duplicate(readable_event_.get(), MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ, out);
    if (status != MX_OK) {
        return status;
    }
    return sizeof(mx_handle_t);
}

mx_status_t VnodeBlob::CopyVmo(mx_rights_t rights, mx_handle_t* out) {
    if (GetState() != kBlobStateReadable) {
        return MX_ERR_BAD_STATE;
    }
    mx_status_t status = InitVmos();
    if (status != MX_OK) {
        return status;
    }

    // TODO(smklein): We could lazily verify more of the VMO if
    // we could fault in pages on-demand.
    //
    // For now, we aggressively verify the entire VMO up front.
    Digest d;
    d = ((const uint8_t*)&digest_[0]);
    auto inode = blobstore_->GetNode(map_index_);
    uint64_t size_merkle = MerkleTree::GetTreeLength(inode->blob_size);
    const void* merkle_data = GetMerkle();
    const void* blob_data = GetData();
    status = MerkleTree::Verify(blob_data, inode->blob_size, merkle_data,
                                size_merkle, 0, inode->blob_size, d);
    if (status != MX_OK) {
        return status;
    }

    // TODO(smklein): Only clone / verify the part of the vmo that
    // was requested.
    const size_t data_start = MerkleTreeBlocks(*inode) * kBlobstoreBlockSize;
    mx_handle_t clone;
    if ((status = mx_vmo_clone(blob_->GetVmo(), MX_VMO_CLONE_COPY_ON_WRITE,
                               data_start, inode->blob_size, &clone)) != MX_OK) {
        return status;
    }

    if ((status = mx_handle_replace(clone, rights, out)) != MX_OK) {
        mx_handle_close(clone);
        return status;
    }
    return MX_OK;
}

mx_status_t VnodeBlob::ReadInternal(void* data, size_t len, size_t off, size_t* actual) {
    if (GetState() != kBlobStateReadable) {
        return MX_ERR_BAD_STATE;
    }

    mx_status_t status = InitVmos();
    if (status != MX_OK) {
        return status;
    }

    Digest d;
    d = ((const uint8_t*)&digest_[0]);
    auto inode = blobstore_->GetNode(map_index_);
    if (off >= inode->blob_size) {
        *actual = 0;
        return MX_OK;
    }
    if (len > (inode->blob_size - off)) {
        len = inode->blob_size - off;
    }

    uint64_t size_merkle = MerkleTree::GetTreeLength(inode->blob_size);
    const void* merkle_data = GetMerkle();
    const void* blob_data = GetData();
    status = MerkleTree::Verify(blob_data, inode->blob_size, merkle_data,
                                size_merkle, off, len, d);
    if (status != MX_OK) {
        return status;
    }

    const size_t data_start = MerkleTreeBlocks(*inode) * kBlobstoreBlockSize;
    return mx_vmo_read(blob_->GetVmo(), data, data_start + off, len, actual);
}

void VnodeBlob::QueueUnlink() {
    flags_ |= kBlobFlagDeletable;
}

// Allocates Blocks IN MEMORY
mx_status_t Blobstore::AllocateBlocks(size_t nblocks, size_t* blkno_out) {
    mx_status_t status;
    if ((status = block_map_.Find(false, 0, block_map_.size(), nblocks, blkno_out)) != MX_OK) {
        // If we have run out of blocks, attempt to add block slices via FVM
        size_t old_size = block_map_.size();
        if (AddBlocks(nblocks) != MX_OK) {
            return MX_ERR_NO_SPACE;
        } else if (block_map_.Find(false, old_size, block_map_.size(), nblocks, blkno_out)
                   != MX_OK) {
            return MX_ERR_NO_SPACE;
        }
    }
    status = block_map_.Set(*blkno_out, *blkno_out + nblocks);
    assert(status == MX_OK);
    info_.alloc_block_count += nblocks;
    *blkno_out += DataStartBlock(info_);
    return MX_OK;
}

// Frees Blocks IN MEMORY
void Blobstore::FreeBlocks(size_t nblocks, size_t blkno) {
    mx_status_t status = block_map_.Clear(blkno - DataStartBlock(info_),
                                          blkno - DataStartBlock(info_) + nblocks);
    info_.alloc_block_count -= nblocks;
    assert(status == MX_OK);
}

// Allocates a node IN MEMORY
mx_status_t Blobstore::AllocateNode(size_t* node_index_out) {
    for (size_t i = 0; i < info_.inode_count; ++i) {
        if (GetNode(i)->start_block == kStartBlockFree) {
            // Found a free node. Mark it as reserved so no one else can allocate it.
            GetNode(i)->start_block = kStartBlockReserved;
            info_.alloc_inode_count++;
            *node_index_out = i;
            return MX_OK;
        }
    }

    // If we didn't find any free inodes, try adding more via FVM.
    size_t old_inode_count = info_.inode_count;
    if (AddInodes() != MX_OK) {
        return MX_ERR_NO_SPACE;
    }

    for (size_t i = old_inode_count; i < info_.inode_count; ++i) {
        if (GetNode(i)->start_block == kStartBlockFree) {
            // Found a free node. Mark it as reserved so no one else can allocate it.
            GetNode(i)->start_block = kStartBlockReserved;
            info_.alloc_inode_count++;
            *node_index_out = i;
            return MX_OK;
        }
    }

    return MX_ERR_NO_SPACE;
}

// Frees a node IN MEMORY
void Blobstore::FreeNode(size_t node_index) {
    memset(GetNode(node_index), 0, sizeof(blobstore_inode_t));
    info_.alloc_inode_count--;
}

mx_status_t Blobstore::Unmount() {
    close(blockfd_);
    // TODO(smklein): To not bind filesystem lifecycle to a process, shut
    // down (closing dispatcher) rather than calling exit.
    exit(0);
    return MX_OK;
}

mx_status_t Blobstore::WriteBitmap(WriteTxn* txn, uint64_t nblocks, uint64_t start_block) {
    uint64_t bbm_start_block = (start_block - DataStartBlock(info_)) / kBlobstoreBlockBits;
    uint64_t bbm_end_block = fbl::roundup(start_block - DataStartBlock(info_) + nblocks,
                                           kBlobstoreBlockBits) / kBlobstoreBlockBits;

    // Write back the block allocation bitmap
    txn->Enqueue(block_map_vmoid_, bbm_start_block, BlockMapStartBlock(info_) + bbm_start_block,
                 bbm_end_block - bbm_start_block);
    return txn->Flush();
}

mx_status_t Blobstore::WriteNode(WriteTxn* txn, size_t map_index) {
    uint64_t b = (map_index * sizeof(blobstore_inode_t)) / kBlobstoreBlockSize;
    txn->Enqueue(node_map_vmoid_, b, NodeMapStartBlock(info_) + b, 1);
    return txn->Flush();
}

mx_status_t Blobstore::NewBlob(const Digest& digest, fbl::RefPtr<VnodeBlob>* out) {
    mx_status_t status;
    // If the blob already exists (or we're having trouble looking up the blob),
    // return an error.
    if ((status = LookupBlob(digest, nullptr)) != MX_ERR_NOT_FOUND) {
        return (status == MX_OK) ? MX_ERR_ALREADY_EXISTS : status;
    }

    fbl::AllocChecker ac;
    *out = fbl::AdoptRef(new (&ac) VnodeBlob(fbl::RefPtr<Blobstore>(this), digest));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }

    hash_.insert(out->get());
    return MX_OK;
}

mx_status_t Blobstore::ReleaseBlob(VnodeBlob* vn) {
    // TODO(smklein): What if kBlobFlagSync is set? Do we risk writing out
    // parts of the blob AFTER it has been deleted?
    // Ex: open, alloc, disk write async start, unlink, release, disk write async end.
    // FWIW, this isn't a problem right now with synchronous writes, but it
    // would become a problem with asynchronous writes.
    switch (vn->GetState()) {
    case kBlobStateEmpty: {
        // There are no in-memory or on-disk structures allocated.
        hash_.erase(*vn);
        return MX_OK;
    }
    case kBlobStateReadable: {
        if (!vn->DeletionQueued()) {
            // We want in-memory and on-disk data to persist.
            hash_.erase(*vn);
            return MX_OK;
        }
        // Fall-through
    }
    case kBlobStateDataWrite:
    case kBlobStateError: {
        vn->SetState(kBlobStateReleasing);
        size_t node_index = vn->GetMapIndex();
        uint64_t start_block = GetNode(node_index)->start_block;
        uint64_t nblocks = GetNode(node_index)->num_blocks;
        FreeNode(node_index);
        FreeBlocks(nblocks, start_block);
        WriteTxn txn(this);
        WriteNode(&txn, node_index);
        WriteBitmap(&txn, nblocks, start_block);
        CountUpdate(&txn);
        hash_.erase(*vn);
        return MX_OK;
    }
    default: {
        assert(false);
    }
    }
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t Blobstore::CountUpdate(WriteTxn* txn) {
    mx_status_t status = MX_OK;
    void* infodata = info_vmo_->GetData();
    memcpy(infodata, &info_, sizeof(info_));
    txn->Enqueue(info_vmoid_, 0, 0, 1);
    return status;
}

typedef struct dircookie {
    size_t index;      // Index into node map
    uint64_t reserved; // Unused
} dircookie_t;

static_assert(sizeof(dircookie_t) <= sizeof(vdircookie_t),
              "Blobstore dircookie too large to fit in IO state");

mx_status_t Blobstore::Readdir(void* cookie, void* dirents, size_t len) {
    fs::DirentFiller df(dirents, len);
    dircookie_t* c = static_cast<dircookie_t*>(cookie);

    for (size_t i = c->index; i < info_.inode_count; ++i) {
        if (GetNode(i)->start_block >= kStartBlockMinimum) {
            Digest digest(GetNode(i)->merkle_root_hash);
            char name[Digest::kLength * 2 + 1];
            mx_status_t r = digest.ToString(name, sizeof(name));
            if (r < 0) {
                return r;
            }
            if ((r = df.Next(name, strlen(name), VTYPE_TO_DTYPE(V_TYPE_FILE))) != MX_OK) {
                break;
            }
            c->index = i + 1;
        }
    }

    return df.BytesFilled();
}

mx_status_t Blobstore::LookupBlob(const Digest& digest, fbl::RefPtr<VnodeBlob>* out) {
    // Look up blob in the fast map (is the blob open elsewhere?)
    fbl::RefPtr<VnodeBlob> vn = fbl::RefPtr<VnodeBlob>(hash_.find(digest.AcquireBytes()).CopyPointer());
    digest.ReleaseBytes();
    if (vn != nullptr) {
        if (out != nullptr) {
            *out = fbl::move(vn);
        }
        return MX_OK;
    }

    // Look up blob in the slow map
    for (size_t i = 0; i < info_.inode_count; ++i) {
        if (GetNode(i)->start_block >= kStartBlockMinimum) {
            if (digest == GetNode(i)->merkle_root_hash) {
                if (out != nullptr) {
                    // Found it. Attempt to wrap the blob in a vnode.
                    fbl::AllocChecker ac;
                    fbl::RefPtr<VnodeBlob> vn =
                        fbl::AdoptRef(new (&ac) VnodeBlob(fbl::RefPtr<Blobstore>(this), digest));
                    if (!ac.check()) {
                        return MX_ERR_NO_MEMORY;
                    }
                    vn->SetState(kBlobStateReadable);
                    vn->SetMapIndex(i);
                    // Delay reading any data from disk until read.
                    hash_.insert(vn.get());
                    *out = fbl::move(vn);
                }
                return MX_OK;
            }
        }
    }
    return MX_ERR_NOT_FOUND;
}

mx_status_t Blobstore::AttachVmo(mx_handle_t vmo, vmoid_t* out) {
    mx_handle_t xfer_vmo;
    mx_status_t status = mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo);
    if (status != MX_OK) {
        return status;
    }
    ssize_t r = ioctl_block_attach_vmo(blockfd_, &xfer_vmo, out);
    if (r < 0) {
        mx_handle_close(xfer_vmo);
        return static_cast<mx_status_t>(r);
    }
    return MX_OK;
}

mx_status_t Blobstore::AddInodes() {
    if (!(info_.flags & kBlobstoreFlagFVM)) {
        return MX_ERR_NO_SPACE;
    }

    const size_t kBlocksPerSlice = info_.slice_size / kBlobstoreBlockSize;
    extend_request_t request;
    request.length = 1;
    request.offset = (kFVMNodeMapStart / kBlocksPerSlice) + info_.ino_slices;
    if (ioctl_block_fvm_extend(blockfd_, &request) < 0) {
        fprintf(stderr, "Blobstore::AddInodes fvm_extend failure");
        return MX_ERR_NO_SPACE;
    }

    const uint32_t kInodesPerSlice = static_cast<uint32_t>(info_.slice_size / kBlobstoreInodeSize);
    uint64_t inodes64 = (info_.ino_slices + static_cast<uint32_t>(request.length))
                        * kInodesPerSlice;
    MX_DEBUG_ASSERT(inodes64 <= fbl::numeric_limits<uint32_t>::max());
    uint32_t inodes = static_cast<uint32_t>(inodes64);
    uint32_t inoblks = (inodes + kBlobstoreInodesPerBlock - 1) / kBlobstoreInodesPerBlock;
    MX_DEBUG_ASSERT(info_.inode_count <= fbl::numeric_limits<uint32_t>::max());
    uint32_t inoblks_old = (static_cast<uint32_t>(info_.inode_count) + kBlobstoreInodesPerBlock - 1)
                           / kBlobstoreInodesPerBlock;
    MX_DEBUG_ASSERT(inoblks_old <= inoblks);

    if (node_map_->Grow(inoblks * kBlobstoreBlockSize) != MX_OK) {
        return MX_ERR_NO_SPACE;
    }

    info_.vslice_count += request.length;
    info_.ino_slices += static_cast<uint32_t>(request.length);
    info_.inode_count = inodes;

    // Reset new inodes to 0
    uintptr_t addr = reinterpret_cast<uintptr_t>(node_map_->GetData());
    memset(reinterpret_cast<void*>(addr + kBlobstoreBlockSize * inoblks_old), 0,
                                   (kBlobstoreBlockSize * (inoblks - inoblks_old)));

    WriteTxn txn(this);
    txn.Enqueue(info_vmoid_, 0, 0, 1);
    txn.Enqueue(node_map_vmoid_, inoblks_old, NodeMapStartBlock(info_) + inoblks_old,
                inoblks - inoblks_old);
    return txn.Flush();
}

mx_status_t Blobstore::AddBlocks(size_t nblocks) {
    if (!(info_.flags & kBlobstoreFlagFVM)) {
        return MX_ERR_NO_SPACE;
    }

    const size_t kBlocksPerSlice = info_.slice_size / kBlobstoreBlockSize;
    extend_request_t request;
    // Number of slices required to add nblocks
    request.length = (nblocks + kBlocksPerSlice - 1) / kBlocksPerSlice;
    request.offset = (kFVMDataStart / kBlocksPerSlice) + info_.dat_slices;

    uint64_t blocks64 = (info_.dat_slices + request.length) * kBlocksPerSlice;
    MX_DEBUG_ASSERT(blocks64 <= fbl::numeric_limits<uint32_t>::max());
    uint32_t blocks = static_cast<uint32_t>(blocks64);
    uint32_t abmblks = (blocks + kBlobstoreBlockBits - 1) / kBlobstoreBlockBits;
    uint64_t abmblks_old = (info_.block_count + kBlobstoreBlockBits - 1) / kBlobstoreBlockBits;
    MX_DEBUG_ASSERT(abmblks_old <= abmblks);

    if (abmblks > kBlocksPerSlice) {
        //TODO(planders): Allocate more slices for the block bitmap.
        fprintf(stderr, "Blobstore::AddBlocks needs to increase block bitmap size");
        return MX_ERR_NO_SPACE;
    }

    if (ioctl_block_fvm_extend(blockfd_, &request) < 0) {
        fprintf(stderr, "Blobstore::AddBlocks FVM Extend failure");
        return MX_ERR_NO_SPACE;
    }

    // Grow the block bitmap to hold new number of blocks
    if (block_map_.Grow(fbl::roundup(blocks, kBlobstoreBlockBits)) != MX_OK) {
        return MX_ERR_NO_SPACE;
    }
    // Grow before shrinking to ensure the underlying storage is a multiple
    // of kBlobstoreBlockSize.
    block_map_.Shrink(blocks);

    WriteTxn txn(this);
    if (abmblks > abmblks_old) {
        txn.Enqueue(block_map_vmoid_, abmblks_old, DataStartBlock(info_) + abmblks_old,
                    abmblks - abmblks_old);
    }

    info_.vslice_count += request.length;
    info_.dat_slices += static_cast<uint32_t>(request.length);
    info_.block_count = blocks;

    txn.Enqueue(info_vmoid_, 0, 0, 1);
    return txn.Flush();
}



Blobstore::Blobstore(int fd, const blobstore_info_t* info)
    : blockfd_(fd) {
    memcpy(&info_, info, sizeof(blobstore_info_t));
}

Blobstore::~Blobstore() {
    if (fifo_client_ != nullptr) {
        ioctl_block_free_txn(blockfd_, &txnid_);
        block_fifo_release_client(fifo_client_);
        ioctl_block_fifo_close(blockfd_);
    }
}

mx_status_t Blobstore::Create(int fd, const blobstore_info_t* info, fbl::RefPtr<Blobstore>* out) {
    mx_status_t status = blobstore_check_info(info, TotalBlocks(*info));
    if (status < 0) {
        fprintf(stderr, "blobstore: Check info failure\n");
        return status;
    }

    fbl::AllocChecker ac;
    fbl::RefPtr<Blobstore> fs = fbl::AdoptRef(new (&ac) Blobstore(fd, info));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }

    mx_handle_t fifo;
    ssize_t r;
    if ((r = ioctl_block_get_fifos(fd, &fifo)) < 0) {
        return static_cast<mx_status_t>(r);
    } else if ((r = ioctl_block_alloc_txn(fd, &fs->txnid_)) < 0) {
        mx_handle_close(fifo);
        return static_cast<mx_status_t>(r);
    } else if ((status = block_fifo_create_client(fifo, &fs->fifo_client_)) != MX_OK) {
        ioctl_block_free_txn(fd, &fs->txnid_);
        mx_handle_close(fifo);
        return status;
    }

    // Keep the block_map_ aligned to a block multiple
    if ((status = fs->block_map_.Reset(BlockMapBlocks(fs->info_) * kBlobstoreBlockBits)) < 0) {
        fprintf(stderr, "blobstore: Could not reset block bitmap\n");
        return status;
    } else if ((status = fs->block_map_.Shrink(fs->info_.block_count)) < 0) {
        fprintf(stderr, "blobstore: Could not shrink block bitmap\n");
        return status;
    }

    size_t nodemap_size = kBlobstoreInodeSize * fs->info_.inode_count;
    MX_DEBUG_ASSERT(fbl::roundup(nodemap_size, kBlobstoreBlockSize) == nodemap_size);
    MX_DEBUG_ASSERT(nodemap_size / kBlobstoreBlockSize == NodeMapBlocks(fs->info_));
    if ((status = MappedVmo::Create(nodemap_size, "nodemap", &fs->node_map_)) != MX_OK) {
        return status;
    } else if ((status = fs->AttachVmo(fs->block_map_.StorageUnsafe()->GetVmo(),
                                       &fs->block_map_vmoid_)) != MX_OK) {
        return status;
    } else if ((status = fs->AttachVmo(fs->node_map_->GetVmo(),
                                       &fs->node_map_vmoid_)) != MX_OK) {
        return status;
    } else if ((status = fs->LoadBitmaps()) < 0) {
        fprintf(stderr, "blobstore: Failed to load bitmaps\n");
        return status;
    } else if ((status = MappedVmo::Create(kBlobstoreBlockSize, "blobstore-superblock",
                                           &fs->info_vmo_)) != MX_OK) {
        fprintf(stderr, "blobstore: Failed to create info vmo\n");
        return status;
    } else if ((status = fs->AttachVmo(fs->info_vmo_->GetVmo(),
                                       &fs->info_vmoid_)) != MX_OK) {
        fprintf(stderr, "blobstore: Failed to attach info vmo\n");
        return status;
    }

    *out = fs;
    return MX_OK;
}

mx_status_t Blobstore::GetRootBlob(fbl::RefPtr<VnodeBlob>* out) {
    fbl::AllocChecker ac;
    fbl::RefPtr<VnodeBlob> vn =
        fbl::AdoptRef(new (&ac) VnodeBlob(fbl::RefPtr<Blobstore>(this)));

    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }

    *out = fbl::move(vn);

    return MX_OK;
}

mx_status_t Blobstore::LoadBitmaps() {
    ReadTxn txn(this);
    txn.Enqueue(block_map_vmoid_, 0, BlockMapStartBlock(info_), BlockMapBlocks(info_));
    txn.Enqueue(node_map_vmoid_, 0, NodeMapStartBlock(info_), NodeMapBlocks(info_));
    return txn.Flush();
}

mx_status_t blobstore_create(fbl::RefPtr<Blobstore>* out, int blockfd) {
    mx_status_t status;

    char block[kBlobstoreBlockSize];
    if ((status = readblk(blockfd, 0, (void*)block)) < 0) {
        fprintf(stderr, "blobstore: could not read info block\n");
        return status;
    }

    blobstore_info_t* info = reinterpret_cast<blobstore_info_t*>(&block[0]);

    uint64_t blocks;
    if ((status = blobstore_get_blockcount(blockfd, &blocks)) != MX_OK) {
        fprintf(stderr, "blobstore: cannot find end of underlying device\n");
        return status;
    } else if ((status = blobstore_check_info(info, blocks)) != MX_OK) {
        fprintf(stderr, "blobstore: Info check failed\n");
        return status;
    }

    if ((status = Blobstore::Create(blockfd, info, out)) != MX_OK) {
        fprintf(stderr, "blobstore: mount failed\n");
        return status;
    }

    return MX_OK;
}

mx_status_t blobstore_mount(fbl::RefPtr<VnodeBlob>* out, int blockfd) {
    mx_status_t status;
    fbl::RefPtr<Blobstore> fs;

    if ((status = blobstore_create(&fs, blockfd)) != MX_OK) {
        return status;
    }

    if ((status = fs->GetRootBlob(out)) != MX_OK) {
        fprintf(stderr, "blobstore: mount failed\n");
        return status;
    }

    return MX_OK;
}
} // namespace blobstore
