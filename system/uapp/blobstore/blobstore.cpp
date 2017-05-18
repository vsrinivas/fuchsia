// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <fs/mxio-dispatcher.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <mxalloc/new.h>
#include <merkle/digest.h>
#include <merkle/tree.h>
#include <mxtl/ref_ptr.h>
#include <mxio/debug.h>

#define MXDEBUG 0

#include "blobstore-private.h"

namespace {

mx_status_t vmo_read_exact(mx_handle_t h, void* data, uint64_t offset, size_t len) {
    size_t actual;
    mx_status_t status = mx_vmo_read(h, data, offset, len, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
    return NO_ERROR;
}

mx_status_t vmo_write_exact(mx_handle_t h, const void* data, uint64_t offset, size_t len) {
    size_t actual;
    mx_status_t status = mx_vmo_write(h, data, offset, len, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
    return NO_ERROR;
}

// Number of blocks reserved for the Merkle Tree
uint64_t MerkleTreeBlocks(const blobstore_inode_t& blobNode) {
    uint64_t size_merkle = merkle::Tree::GetTreeLength(blobNode.blob_size);
    return mxtl::roundup(size_merkle, kBlobstoreBlockSize) / kBlobstoreBlockSize;
}

// Get a pointer to the nth block of the bitmap.
inline void* get_raw_bitmap_data(const RawBitmap& bm, uint64_t n) {
    assert(n * kBlobstoreBlockSize < bm.size()); // Accessing beyond end of bitmap
    assert(kBlobstoreBlockSize <= (n + 1) * kBlobstoreBlockSize); // Avoid overflow
    return (void*)((uintptr_t)(bm.StorageUnsafe()->GetData()) +
                   (uintptr_t)(kBlobstoreBlockSize * n));
}

// Read data from disk at block 'bno', into the 'nth' logical block of the vmo.
mx_status_t vn_fill_block(int fd, mx_handle_t vmo, uint64_t n, uint64_t bno) {
    // TODO(smklein): read directly from block device into vmo; no need to copy
    // into an intermediate buffer.
    char bdata[kBlobstoreBlockSize];
    if (blobstore::readblk(fd, bno, bdata) != NO_ERROR) {
        return ERR_IO;
    }
    mx_status_t status = vmo_write_exact(vmo, bdata, n * kBlobstoreBlockSize, kBlobstoreBlockSize);
    if (status != NO_ERROR) {
        return status;
    }
    return NO_ERROR;
}

// Write data to disk at block 'bno', from the 'nth' logical block of the vmo.
mx_status_t vn_dump_block(int fd, mx_handle_t vmo, uint64_t n, uint64_t bno, bool partial) {
    // TODO(smklein): read directly into block device from vmo; no need to copy
    // into an intermediate buffer.
    char bdata[kBlobstoreBlockSize];
    size_t actual;
    mx_status_t status = mx_vmo_read(vmo, bdata, n * kBlobstoreBlockSize,
                                     kBlobstoreBlockSize, &actual);
    if (status != NO_ERROR) {
        return status;
    }

    if (partial)  {
        // It's okay to read a partial block -- set the rest to 'zero'.
        memset(bdata + actual, 0, sizeof(bdata) - actual);
    } else if (actual != kBlobstoreBlockSize) {
        // We should have been able to read the whole block.
        return ERR_IO;
    }

    if (blobstore::writeblk(fd, bno, bdata) != NO_ERROR) {
        return ERR_IO;
    }
    return NO_ERROR;
}

// Sanity check the metadata for the blobstore, given a maximum number of
// available blocks.
mx_status_t blobstore_check_info(const blobstore_info_t* info, uint64_t max) {
    if ((info->magic0 != kBlobstoreMagic0) ||
        (info->magic1 != kBlobstoreMagic1)) {
        fprintf(stderr, "blobstore: bad magic\n");
        return ERR_INVALID_ARGS;
    }
    if (info->version != kBlobstoreVersion) {
        fprintf(stderr, "blobstore: FS Version: %08x. Driver version: %08x\n", info->version,
              kBlobstoreVersion);
        return ERR_INVALID_ARGS;
    }
    if (info->block_size != kBlobstoreBlockSize) {
        fprintf(stderr, "blobstore: bsz %u unsupported\n", info->block_size);
        return ERR_INVALID_ARGS;
    }
    if (info->block_count > max) {
        fprintf(stderr, "blobstore: too large for device\n");
        return ERR_INVALID_ARGS;
    }
    if (info->blob_header_next != 0) {
        fprintf(stderr, "blobstore: linked blob headers not yet supported\n");
        return ERR_INVALID_ARGS;
    }
    return NO_ERROR;
}

} // namespace

namespace blobstore {

mxtl::unique_ptr<fs::Dispatcher> blobstore_global_dispatcher;

fs::Dispatcher* VnodeBlob::GetDispatcher() {
    return blobstore_global_dispatcher.get();
}

void* Blobstore::GetBlockmapData(uint64_t n) const {
    assert(n < BlockMapBlocks(info_));
    return get_raw_bitmap_data(block_map_, n);
}

// Get a pointer to the nth block of the node map.
void* Blobstore::GetNodemapData(uint64_t n) const {
    assert(n < NodeMapBlocks(info_));
    return (void*)((uintptr_t)(node_map_.get()) + (uintptr_t)(kBlobstoreBlockSize * n));
}

mx_status_t readblk(int fd, uint64_t bno, void* data) {
    off_t off = bno * kBlobstoreBlockSize;
    if (lseek(fd, off, SEEK_SET) < 0) {
        fprintf(stderr, "blobstore: cannot seek to block %lu\n", bno);
        return ERR_IO;
    }
    if (read(fd, data, kBlobstoreBlockSize) != kBlobstoreBlockSize) {
        fprintf(stderr, "blobstore: cannot read block %lu\n", bno);
        return ERR_IO;
    }
    return NO_ERROR;
}

mx_status_t writeblk(int fd, uint64_t bno, const void* data) {
    off_t off = bno * kBlobstoreBlockSize;
    if (lseek(fd, off, SEEK_SET) < 0) {
        fprintf(stderr, "blobstore: cannot seek to block %lu\n", bno);
        return ERR_IO;
    }
    if (write(fd, data, kBlobstoreBlockSize) != kBlobstoreBlockSize) {
        fprintf(stderr, "blobstore: cannot write block %lu\n", bno);
        return ERR_IO;
    }
    return NO_ERROR;
}

mx_status_t VnodeBlob::InitVmos() {
    if (blob_ != nullptr) {
        return NO_ERROR;
    }

    mx_status_t status;
    int fd = blobstore_->blockfd_;
    blobstore_inode_t* inode = &blobstore_->node_map_[map_index_];
    uint64_t merkle_vmo_size = MerkleTreeBlocks(*inode) * kBlobstoreBlockSize;
    uint64_t data_vmo_size = BlobDataBlocks(*inode) * kBlobstoreBlockSize;

    if (merkle_vmo_size != 0) {
        if ((status = MappedVmo::Create(merkle_vmo_size, &merkle_tree_)) != NO_ERROR) {
            error("Failed to initialize vmo; error: %d\n", status);
            goto fail;
        }

        for (uint64_t n = 0; n < MerkleTreeBlocks(*inode); n++) {
            uint64_t bno = inode->start_block + n;
            if ((status = vn_fill_block(fd, merkle_tree_->GetVmo(), n, bno)) != NO_ERROR) {
                error("Failed to fill bno\n");
                goto fail;
            }
        }
    }

    if ((status = MappedVmo::Create(data_vmo_size, &blob_)) != NO_ERROR) {
        error("Failed to initialize vmo; error: %d\n", status);
        goto fail;
    }

    for (uint64_t n = 0; n < BlobDataBlocks(*inode); n++) {
        uint64_t bno = inode->start_block + n + MerkleTreeBlocks(*inode);
        if ((status = vn_fill_block(fd, blob_->GetVmo(), n, bno)) != NO_ERROR) {
            error("Failed to fill bno\n");
            goto fail;
        }
    }

    return NO_ERROR;
fail:
    BlobCloseHandles();
    return status;
}

uint64_t VnodeBlob::SizeData() const {
    if (GetState() == kBlobStateReadable) {
        auto inode = &blobstore_->node_map_[map_index_];
        return inode->blob_size;
    }
    return 0;
}

VnodeBlob::VnodeBlob(mxtl::RefPtr<Blobstore> bs, const merkle::Digest& digest) :
    blobstore_(mxtl::move(bs)),
    bytes_written_(0),
    flags_(kBlobStateEmpty) {

    digest.CopyTo(digest_, sizeof(digest_));
}

VnodeBlob::VnodeBlob(mxtl::RefPtr<Blobstore> bs) :
    blobstore_(mxtl::move(bs)),
    bytes_written_(0),
    flags_(kBlobStateEmpty | kBlobFlagDirectory) {}

void VnodeBlob::BlobCloseHandles() {
    merkle_tree_ = nullptr;
    blob_ = nullptr;
    readable_event_.reset();
}

mx_status_t VnodeBlob::SpaceAllocate(uint64_t size_data) {
    if (size_data == 0) {
        return ERR_INVALID_ARGS;
    }
    if (GetState() != kBlobStateEmpty) {
        return ERR_BAD_STATE;
    }

    // Find a free node, mark it as reserved.
    mx_status_t status;
    if ((status = blobstore_->AllocateNode(&map_index_)) != NO_ERROR) {
        return status;
    }

    // Initialize the inode with known fields
    blobstore_inode_t* inode = &blobstore_->node_map_[map_index_];
    memset(inode->merkle_root_hash, 0, merkle::Digest::kLength);
    inode->blob_size = size_data;
    inode->num_blocks = MerkleTreeBlocks(*inode) + BlobDataBlocks(*inode);

    // Open VMOs, so we can begin writing after allocate succeeds.
    uint64_t size_merkle = merkle::Tree::GetTreeLength(size_data);
    if (size_merkle != 0) {
        if ((status = MappedVmo::Create(size_merkle, &merkle_tree_)) != NO_ERROR) {
            goto fail;
        }
    }
    if ((status = MappedVmo::Create(size_data, &blob_)) != NO_ERROR) {
        goto fail;
    }

    // Allocate space for the blob
    if ((status = blobstore_->AllocateBlocks(inode->num_blocks, &inode->start_block)) != NO_ERROR) {
        goto fail;
    }

    SetState(kBlobStateDataWrite);
    return NO_ERROR;

fail:
    BlobCloseHandles();
    blobstore_->FreeNode(map_index_);
    return status;
}

// A helper function for dumping either the Merkle Tree or the actual blob data
// to both (1) The containing VMO, and (2) disk.
mx_status_t VnodeBlob::WriteShared(size_t start, size_t len, uint64_t maxlen,
                                   mx_handle_t vmo, uint64_t start_block) {
    // Write as many 'entire blocks' as possible
    uint64_t n = start / kBlobstoreBlockSize;
    uint64_t n_end = (start + len) / kBlobstoreBlockSize;
    mx_status_t status;
    while (n < n_end) {
        status = vn_dump_block(blobstore_->blockfd_, vmo, n, n + start_block, false);
        if (status != NO_ERROR) {
            return status;
        }
        n++;
    }

    // Special case: We've written all the 'whole blocks', but we're missing
    // a partial block at the very end.
    if ((start + len == maxlen) &&
        (maxlen % kBlobstoreBlockSize != 0)) {
        status = vn_dump_block(blobstore_->blockfd_, vmo, n, n + start_block, true);
        if (status != NO_ERROR) {
            return status;
        }
    }

    assert(start + len <= maxlen);
    return NO_ERROR;
}

mx_status_t VnodeBlob::WriteMetadata() {
    assert(GetState() == kBlobStateDataWrite);

    // All data has been written to the containing VMO
    SetState(kBlobStateReadable);
    if (readable_event_.is_valid()) {
        mx_status_t status = readable_event_.signal(0u, MX_USER_SIGNAL_0);
        if (status != NO_ERROR) {
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
    auto inode = &blobstore_->node_map_[map_index_];

    // Write block allocation bitmap
    if (blobstore_->WriteBitmap(inode->num_blocks, inode->start_block) != NO_ERROR) {
        return ERR_IO;
    }

    // Flush the block allocation bitmap to disk
    fsync(blobstore_->blockfd_);

    // Update the on-disk hash
    memcpy(inode->merkle_root_hash, &digest_[0], merkle::Digest::kLength);

    // Write back the blob node
    if (blobstore_->WriteNode(map_index_)) {
        return ERR_IO;
    }

    flags_ &= ~kBlobFlagSync;
    return NO_ERROR;
}

mx_status_t VnodeBlob::WriteInternal(const void* data, size_t len, size_t* actual) {
    *actual = 0;
    if (len == 0) {
        return NO_ERROR;
    }

    auto inode = &blobstore_->node_map_[map_index_];

    if (GetState() == kBlobStateDataWrite) {
        size_t to_write = mxtl::min(len, inode->blob_size - bytes_written_);
        mx_status_t status = vmo_write_exact(blob_->GetVmo(), data,
                                             bytes_written_, to_write);
        if (status != NO_ERROR) {
            return status;
        }

        status = WriteShared(bytes_written_, len, inode->blob_size,
                             blob_->GetVmo(),
                             inode->start_block + MerkleTreeBlocks(*inode));
        if (status != NO_ERROR) {
            SetState(kBlobStateError);
            return status;
        }

        *actual = to_write;
        bytes_written_ += to_write;

        // More data to write.
        if (bytes_written_ < inode->blob_size) {
            return NO_ERROR;
        }

        // TODO(smklein): As an optimization, use the CreateInit/Update/Final
        // methods to create the merkle tree as we write data, rather than
        // waiting until the data is fully downloaded to create the tree.
        merkle::Tree tree;
        size_t merkle_size = tree.GetTreeLength(inode->blob_size);
        if (merkle_size > 0) {
            merkle::Digest digest;
            if (tree.Create(blob_->GetData(), inode->blob_size, merkle_tree_->GetData(),
                            merkle_size, &digest) != NO_ERROR) {
                SetState(kBlobStateError);
                return status;
            } else if (digest != digest_) {
                // Downloaded blob did not match provided digest
                SetState(kBlobStateError);
                return status;
            }

            status = WriteShared(0, merkle_size, merkle_size, merkle_tree_->GetVmo(),
                                 inode->start_block);
            if (status != NO_ERROR) {
                SetState(kBlobStateError);
                return status;
            }
        }

        // No more data to write. Flush to disk.
        if ((status = WriteMetadata()) != NO_ERROR) {
            SetState(kBlobStateError);
            return status;
        }
        return NO_ERROR;
    }

    return ERR_BAD_STATE;
}

mx_status_t VnodeBlob::GetReadableEvent(mx_handle_t* out) {
    mx_status_t status;
    // This is the first 'wait until read event' request received
    if (!readable_event_.is_valid()) {
        status = mx::event::create(0, &readable_event_);
        if (status != NO_ERROR) {
            return status;
        } else if (GetState() == kBlobStateReadable) {
            readable_event_.signal(0u, MX_USER_SIGNAL_0);
        }
    }
    status = mx_handle_duplicate(readable_event_.get(), MX_RIGHT_DUPLICATE |
                                 MX_RIGHT_TRANSFER | MX_RIGHT_READ, out);
    if (status != NO_ERROR) {
        return status;
    }
    return sizeof(mx_handle_t);
}

mx_status_t VnodeBlob::CopyVmo(mx_rights_t rights, mx_handle_t* out) {
    if (GetState() != kBlobStateReadable) {
        return ERR_BAD_STATE;
    }
    mx_status_t status = InitVmos();
    if (status != NO_ERROR) {
        return status;
    }

    // TODO(smklein): We could lazily verify more of the VMO if:
    // 1) We could fault in pages on-demand, or
    // 2) We could create a COW subsection of the original VMO.
    //
    // For now, we aggressively verify the entire VMO up front.
    merkle::Tree mt;
    merkle::Digest d;
    d = ((const uint8_t*) &digest_[0]);
    auto inode = &blobstore_->node_map_[map_index_];
    uint64_t size_merkle = merkle::Tree::GetTreeLength(inode->blob_size);
    const void* merkle_data = (merkle_tree_ != nullptr) ? merkle_tree_->GetData() : nullptr;
    status = mt.Verify(blob_->GetData(), inode->blob_size,
                       merkle_data, size_merkle,
                       0, inode->blob_size, d);
    if (status != NO_ERROR) {
        return status;
    }

    return mx_handle_duplicate(blob_->GetVmo(), rights, out);
}

mx_status_t VnodeBlob::ReadInternal(void* data, size_t len, size_t off, size_t* actual) {
    if (GetState() != kBlobStateReadable) {
        return ERR_BAD_STATE;
    }

    mx_status_t status = InitVmos();
    if (status != NO_ERROR) {
        return status;
    }

    merkle::Tree mt;
    merkle::Digest d;
    d = ((const uint8_t*) &digest_[0]);
    auto inode = &blobstore_->node_map_[map_index_];
    uint64_t size_merkle = merkle::Tree::GetTreeLength(inode->blob_size);
    const void* merkle_data = (merkle_tree_ != nullptr) ? merkle_tree_->GetData() : nullptr;
    status = mt.Verify(blob_->GetData(), inode->blob_size,
                       merkle_data, size_merkle,
                       off, len, d);
    if (status != NO_ERROR) {
        return status;
    }

    return mx_vmo_read(blob_->GetVmo(), data, off, len, actual);
}

void VnodeBlob::QueueUnlink() {
    flags_ |= kBlobFlagDeletable;
}

// Allocates Blocks IN MEMORY
mx_status_t Blobstore::AllocateBlocks(size_t nblocks, size_t* blkno_out) {
    mx_status_t status;
    if ((status = block_map_.Find(false, 0, block_map_.size(), nblocks, blkno_out)) != NO_ERROR) {
        return ERR_NO_SPACE;
    }
    assert(DataStartBlock(info_) <= *blkno_out);
    status = block_map_.Set(*blkno_out, *blkno_out + nblocks);
    assert(status == NO_ERROR);
    return NO_ERROR;
}

// Frees Blocks IN MEMORY
void Blobstore::FreeBlocks(size_t nblocks, size_t blkno) {
    assert(DataStartBlock(info_) <= blkno);
    mx_status_t status = block_map_.Clear(blkno, blkno + nblocks);
    assert(status == NO_ERROR);
}

// Allocates a node IN MEMORY
mx_status_t Blobstore::AllocateNode(size_t* node_index_out) {
    for (size_t i = 0; i < info_.inode_count; ++i) {
        if (node_map_[i].start_block == kStartBlockFree) {
            // Found a free node. Mark it as reserved so no one else can allocate it.
            node_map_[i].start_block = kStartBlockReserved;
            *node_index_out = i;
            return NO_ERROR;
        }
    }
    return ERR_NO_RESOURCES;
}

// Frees a node IN MEMORY
void Blobstore::FreeNode(size_t node_index) {
    memset(&node_map_[node_index], 0, sizeof(blobstore_inode_t));
}

mx_status_t Blobstore::Unmount() {
    close(blockfd_);
    return NO_ERROR;
}

mx_status_t Blobstore::WriteBitmap(uint64_t nblocks, uint64_t start_block) {
    uint64_t bbm_start_block = (start_block) / kBlobstoreBlockBits;
    uint64_t bbm_end_block = mxtl::roundup(start_block + nblocks, kBlobstoreBlockBits) /
            kBlobstoreBlockBits;

    // Write back the block allocation bitmap
    for (uint64_t b = bbm_start_block; b < bbm_end_block; b++) {
        void* data = GetBlockmapData(b);
        if (writeblk(blockfd_, BlockMapStartBlock() + b, data) != NO_ERROR) {
            return ERR_IO;
        }
    }
    return NO_ERROR;
}

mx_status_t Blobstore::WriteNode(size_t map_index) {
    uint64_t b = (map_index * sizeof(blobstore_inode_t)) / kBlobstoreBlockSize;
    void* data = GetNodemapData(b);
    if (writeblk(blockfd_, NodeMapStartBlock(info_) + b, data) != NO_ERROR) {
        return ERR_IO;
    }
    return NO_ERROR;
}

mx_status_t Blobstore::NewBlob(const merkle::Digest& digest, mxtl::RefPtr<VnodeBlob>* out) {
    mx_status_t status;
    // If the blob already exists (or we're having trouble looking up the blob),
    // return an error.
    if ((status = LookupBlob(digest, nullptr)) != ERR_NOT_FOUND) {
        return (status == NO_ERROR) ? ERR_ALREADY_EXISTS : status;
    }

    AllocChecker ac;
    *out = mxtl::AdoptRef(new (&ac) VnodeBlob(mxtl::RefPtr<Blobstore>(this), digest));
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }

    hash_.insert(out->get());
    return NO_ERROR;
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
            return NO_ERROR;
        }
        case kBlobStateReadable: {
            if (!vn->DeletionQueued()) {
                // We want in-memory and on-disk data to persist.
                hash_.erase(*vn);
                return NO_ERROR;
            }
            // Fall-through
        }
        case kBlobStateDataWrite:
        case kBlobStateError: {
            vn->SetState(kBlobStateReleasing);
            size_t node_index = vn->GetMapIndex();
            uint64_t start_block = node_map_[node_index].start_block;
            uint64_t nblocks = node_map_[node_index].num_blocks;
            FreeNode(node_index);
            FreeBlocks(nblocks, start_block);
            WriteNode(node_index);
            WriteBitmap(nblocks, start_block);
            hash_.erase(*vn);
            return NO_ERROR;
        }
        default: {
            assert(false);
        }
    }
    return ERR_NOT_SUPPORTED;
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
        if (node_map_[i].start_block >= kStartBlockMinimum) {
            merkle::Digest digest(node_map_[i].merkle_root_hash);
            char name[merkle::Digest::kLength * 2 + 1];
            mx_status_t r = digest.ToString(name, sizeof(name));
            if (r < 0) {
                return r;
            }
            if ((r = df.Next(name, strlen(name), VTYPE_TO_DTYPE(V_TYPE_FILE))) != NO_ERROR) {
                break;
            }
            c->index = i + 1;
        }
    }

    return df.BytesFilled();
}

mx_status_t Blobstore::LookupBlob(const merkle::Digest& digest, mxtl::RefPtr<VnodeBlob>* out) {
    // Look up blob in the fast map (is the blob open elsewhere?)
    mxtl::RefPtr<VnodeBlob> vn = mxtl::RefPtr<VnodeBlob>(hash_.find(digest.AcquireBytes()).CopyPointer());
    digest.ReleaseBytes();
    if (vn != nullptr) {
        if (out != nullptr) {
            *out = mxtl::move(vn);
        }
        return NO_ERROR;
    }

    // Look up blob in the slow map
    for (size_t i = 0; i < info_.inode_count; ++i) {
        if (node_map_[i].start_block >= kStartBlockMinimum) {
            if (digest == node_map_[i].merkle_root_hash) {
                if (out != nullptr) {
                    // Found it. Attempt to wrap the blob in a vnode.
                    AllocChecker ac;
                    mxtl::RefPtr<VnodeBlob> vn =
                            mxtl::AdoptRef(new (&ac) VnodeBlob(mxtl::RefPtr<Blobstore>(this), digest));
                    if (!ac.check()) {
                        return ERR_NO_MEMORY;
                    }
                    vn->SetState(kBlobStateReadable);
                    vn->SetMapIndex(i);
                    // Delay reading any data from disk until read.
                    hash_.insert(vn.get());
                    *out = mxtl::move(vn);
                }
                return NO_ERROR;
            }
        }
    }
    return ERR_NOT_FOUND;
}

Blobstore::Blobstore(int fd, const blobstore_info_t* info) : blockfd_(fd) {
    memcpy(&info_, info, sizeof(blobstore_info_t));
}

Blobstore::~Blobstore() {}

mx_status_t Blobstore::Create(int fd, const blobstore_info_t* info, mxtl::RefPtr<VnodeBlob>* out) {
    uint64_t blocks = info->block_count;

    mx_status_t status = blobstore_check_info(info, blocks);
    if (status < 0) {
        fprintf(stderr, "blobstore: Check info failure\n");
        return status;
    }

    if ((status = fs::MxioDispatcher::Create(&blobstore_global_dispatcher)) != NO_ERROR) {
        return status;
    }
    AllocChecker ac;
    mxtl::RefPtr<Blobstore> fs = mxtl::AdoptRef(new (&ac) Blobstore(fd, info));
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }

    // Keep the block_map_ aligned to a block multiple
    if ((status = fs->block_map_.Reset(BlockMapBlocks(fs->info_) * kBlobstoreBlockBits)) < 0) {
        fprintf(stderr, "blobstore: Could not reset block bitmap\n");
        return status;
    } else if ((status = fs->block_map_.Shrink(fs->info_.block_count)) < 0) {
        fprintf(stderr, "blobstore: Could not shrink block bitmap\n");
        return status;
    }

    auto nodemap = new (&ac) blobstore_inode_t[fs->info_.inode_count];
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    fs->node_map_.reset(mxtl::move(nodemap));

    if ((status = fs->LoadBitmaps()) < 0) {
        fprintf(stderr, "blobstore: Failed to load bitmaps\n");
        return status;
    }

    *out = mxtl::AdoptRef(new (&ac) VnodeBlob(mxtl::move(fs)));
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    return NO_ERROR;
}

mx_status_t Blobstore::LoadBitmaps() {
    uint64_t bbm_blocks = BlockMapBlocks(info_);
    uint64_t nbm_blocks = NodeMapBlocks(info_);

    for (uint64_t n = 0; n < bbm_blocks; n++) {
        if (readblk(blockfd_, BlockMapStartBlock() + n, GetBlockmapData(n))) {
            fprintf(stderr, "blobstore: failed reading alloc bitmap\n");
            return ERR_IO;
        }
    }
    for (uint64_t n = 0; n < nbm_blocks; n++) {
        if (readblk(blockfd_, NodeMapStartBlock(info_) + n, GetNodemapData(n))) {
            fprintf(stderr, "blobstore: failed reading inode map\n");
            return ERR_IO;
        }
    }
    return NO_ERROR;
}

mx_status_t blobstore_mount(mxtl::RefPtr<VnodeBlob>* out, int blockfd) {
    mx_status_t status;
    struct stat s;

    char block[kBlobstoreBlockSize];
    if ((status = readblk(blockfd, 0, (void*) block)) < 0) {
        fprintf(stderr, "blobstore: could not read info block\n");
        return status;
    }

    blobstore_info_t* info = reinterpret_cast<blobstore_info_t*>(&block[0]);

    if (fstat(blockfd, &s) < 0) {
        fprintf(stderr, "blobstore: cannot find end of underlying device\n");
        return ERR_BAD_STATE;
    } else if ((status = blobstore_check_info(info, s.st_size / kBlobstoreBlockSize)) != NO_ERROR) {
        fprintf(stderr, "blobstore: Info check failed\n");
        return status;
    } else if ((status = Blobstore::Create(blockfd, info, out)) != NO_ERROR) {
        fprintf(stderr, "blobstore: mount failed\n");
        return status;
    }

    return NO_ERROR;
}

int blobstore_mkfs(int fd) {
    struct stat s;
    if (fstat(fd, &s) < 0) {
        fprintf(stderr, "blobstore: cannot find end of underlying device\n");
        return ERR_BAD_STATE;
    }

    uint64_t blocks = s.st_size / kBlobstoreBlockSize;
    uint64_t inodes = 32768;

    blobstore_info_t info;
    memset(&info, 0x00, sizeof(info));
    info.magic0 = kBlobstoreMagic0;
    info.magic1 = kBlobstoreMagic1;
    info.version = kBlobstoreVersion;
    info.flags = kBlobstoreFlagClean;
    info.block_size = kBlobstoreBlockSize;
    info.block_count = blocks;
    info.inode_count = inodes;
    info.blob_header_next = 0; // TODO(smklein): Allow chaining

    xprintf("Blobstore Mkfs\n");
    xprintf("Disk size  : %llu\n", s.st_size);
    xprintf("Block Size : %u\n", kBlobstoreBlockSize);
    xprintf("Block Count: %lu\n", blocks);
    xprintf("Inode Count: %lu\n", inodes);

    // Determine the number of blocks necessary for the block map and node map.
    uint64_t bbm_blocks = BlockMapBlocks(info);
    uint64_t nbm_blocks = NodeMapBlocks(info);
    RawBitmap abm;
    if (abm.Reset(bbm_blocks * kBlobstoreBlockBits)) {
        fprintf(stderr, "Couldn't allocate blobstore block map\n");
        return -1;
    } else if (abm.Shrink(info.block_count)) {
        fprintf(stderr, "Couldn't shrink blobstore block map\n");
        return -1;
    }

    if (info.inode_count * sizeof(blobstore_inode_t) != nbm_blocks * kBlobstoreBlockSize) {
        fprintf(stderr, "For simplicity, inode table block must be entirely filled\n");
        return -1;
    }

    // update block bitmap:
    // reserve all blocks before the data storage area.
    abm.Set(0, DataStartBlock(info));

    // All in-memory structures have been created successfully. Dump everything to disk.
    char block[kBlobstoreBlockSize];
    mx_status_t status;

    // write the root block to disk
    memset(block, 0, sizeof(block));
    memcpy(block, &info, sizeof(info));
    if ((status = writeblk(fd, 0, block)) != NO_ERROR) {
        fprintf(stderr, "Failed to write root block\n");
        return status;
    }

    // write allocation bitmap to disk
    for (uint64_t n = 0; n < bbm_blocks; n++) {
        void* bmdata = get_raw_bitmap_data(abm, n);
        if ((status = writeblk(fd, BlockMapStartBlock() + n, bmdata)) < 0) {
            fprintf(stderr, "Failed to write blockmap block %lu\n", n);
            return status;
        }
    }

    // write node map to disk
    for (uint64_t n = 0; n < nbm_blocks; n++) {
        memset(block, 0, sizeof(block));
        if (writeblk(fd, NodeMapStartBlock(info) + n, block)) {
            fprintf(stderr, "blobstore: failed writing inode map\n");
            return ERR_IO;
        }
    }

    xprintf("BLOBSTORE: mkfs success\n");
    return 0;
}

} // namespace blobstore
