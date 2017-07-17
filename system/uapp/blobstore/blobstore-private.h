// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "blobstore.h"

#include <string.h>

#include <bitmap/raw-bitmap.h>
#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>

#ifdef __Fuchsia__
#include <block-client/client.h>
#include <fs/mapped-vmo.h>
#include <zx/event.h>
#include <zx/vmo.h>
#endif

namespace blobstore {

class Blobstore;
class VnodeBlob;

using WriteTxn = fs::WriteTxn<kBlobstoreBlockSize, Blobstore>;
using ReadTxn = fs::ReadTxn<kBlobstoreBlockSize, Blobstore>;
using digest::Digest;

typedef uint32_t BlobFlags;

// clang-format off

// After Open;
constexpr BlobFlags kBlobStateEmpty       = 0x00000001; // Not yet allocated
// After Ioctl configuring size:
constexpr BlobFlags kBlobStateDataWrite   = 0x00000002; // Data is being written
// After Writing:
constexpr BlobFlags kBlobStateReadable    = 0x00000004; // Readable
// After Unlink:
constexpr BlobFlags kBlobStateReleasing   = 0x00000008; // In the process of unlinking
// Unrecoverable error state:
constexpr BlobFlags kBlobStateError       = 0x00000010; // Unrecoverable error state
constexpr BlobFlags kBlobStateMask        = 0x000000FF;

// Informational non-state flags:
constexpr BlobFlags kBlobFlagSync         = 0x00000100; // The blob is being written to disk
constexpr BlobFlags kBlobFlagDeletable    = 0x00000200; // This node should be unlinked when closed
constexpr BlobFlags kBlobFlagDirectory    = 0x00000400; // This node represents the root directory
constexpr BlobFlags kBlobOtherMask        = 0x0000FF00;

// clang-format on

#ifdef __Fuchsia__

class VnodeBlob final : public fs::Vnode {
public:
    // Intrusive methods and structures
    using WAVLTreeNodeState = fbl::WAVLTreeNodeState<VnodeBlob*>;
    struct TypeWavlTraits {
        static WAVLTreeNodeState& node_state(VnodeBlob& b) { return b.type_wavl_state_; }
    };
    const uint8_t* GetKey() const {
        return &digest_[0];
    };

    BlobFlags GetState() const {
        return flags_ & kBlobStateMask;
    }

    bool IsDirectory() const { return flags_ & kBlobFlagDirectory; }

    zx_status_t Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                      void* out_buf, size_t out_len, size_t* out_actual) final;

    bool DeletionQueued() const {
        return flags_ & kBlobFlagDeletable;
    }

    void SetState(BlobFlags new_state) {
        flags_ = (flags_ & ~kBlobStateMask) | new_state;
    }

    size_t GetMapIndex() const {
        return map_index_;
    }

    void SetMapIndex(size_t i) {
        map_index_ = i;
    }

    uint64_t SizeData() const;

    // Constructs the "directory" blob
    VnodeBlob(fbl::RefPtr<Blobstore> bs);
    // Constructs actual blobs
    VnodeBlob(fbl::RefPtr<Blobstore> bs, const Digest& digest);
    virtual ~VnodeBlob();

private:
    friend struct TypeWavlTraits;

    DISALLOW_COPY_ASSIGN_AND_MOVE(VnodeBlob);

    void BlobCloseHandles();

    // Returns a handle to an event which will be signalled when
    // the blob is readable.
    //
    // Returns "ZX_OK" if blob is already readable.
    // Otherwise, returns size of the handle.
    zx_status_t GetReadableEvent(zx_handle_t* out);

    zx_status_t CopyVmo(zx_rights_t rights, zx_handle_t* out);

    void QueueUnlink();

    // If successful, allocates Blob Node and Blocks (in-memory)
    // kBlobStateEmpty --> kBlobStateDataWrite
    zx_status_t SpaceAllocate(uint64_t size_data);

    // Writes to either the Merkle Tree or the Data section,
    // depending on the state.
    zx_status_t WriteInternal(const void* data, size_t len, size_t* actual);

    // Reads from a blob.
    // Requires: kBlobStateReadable
    zx_status_t ReadInternal(void* data, size_t len, size_t off, size_t* actual);

    // Vnode I/O operations
    zx_status_t GetHandles(uint32_t flags, zx_handle_t* hnds, size_t* hcount, uint32_t* type,
                           void* extra, uint32_t* esize) final;
    zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;
    zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len) final;
    zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
    zx_status_t Write(const void* data, size_t len, size_t offset,
                      size_t* out_actual) final;
    zx_status_t Append(const void* data, size_t len, size_t* out_end,
                       size_t* out_actual) final;
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name,
                       uint32_t mode) final;
    zx_status_t Truncate(size_t len) final;
    zx_status_t Unlink(fbl::StringPiece name, bool must_be_dir) final;
    zx_status_t Mmap(int flags, size_t len, size_t* off, zx_handle_t* out) final;
    zx_status_t Sync() final;

    // Read both VMOs into memory, if we haven't already.
    //
    // TODO(smklein): When we have can register the Blob Store as a pager
    // service, and it can properly handle pages faults on a vnode's contents,
    // then we can avoid reading the entire blob up-front. Until then, read
    // the contents of a VMO into memory when it is opened.
    zx_status_t InitVmos();

    zx_status_t WriteShared(WriteTxn* txn, size_t start, size_t len, uint64_t start_block);
    // Called by Blob once the last write has completed, updating the
    // on-disk metadata.
    zx_status_t WriteMetadata();

    // Acquire a pointer to the mapped data or merkle tree
    void* GetData() const;
    void* GetMerkle() const;

    WAVLTreeNodeState type_wavl_state_{};

    const fbl::RefPtr<Blobstore> blobstore_;
    BlobFlags flags_{};

    // The blob_ here consists of:
    // 1) The Merkle Tree
    // 2) The Blob itself, aligned to the nearest kBlobstoreBlockSize
    fbl::unique_ptr<MappedVmo> blob_{};
    vmoid_t vmoid_{};

    zx::event readable_event_{};
    uint64_t bytes_written_{};
    uint8_t digest_[Digest::kLength]{};

    size_t map_index_{};
};

// We need to define this structure to allow the Blob to be indexable by a key
// which is larger than a primitive type: the keys are 'Digest::kLength'
// bytes long.
struct MerkleRootTraits {
    static const uint8_t* GetKey(const VnodeBlob& obj) { return obj.GetKey(); }
    static bool LessThan(const uint8_t* k1, const uint8_t* k2) {
        return memcmp(k1, k2, Digest::kLength) < 0;
    }
    static bool EqualTo(const uint8_t* k1, const uint8_t* k2) {
        return memcmp(k1, k2, Digest::kLength) == 0;
    }
};

class Blobstore : public fbl::RefCounted<Blobstore> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Blobstore);
    friend class VnodeBlob;

    static zx_status_t Create(int blockfd, const blobstore_info_t* info, fbl::RefPtr<Blobstore>* out);

    zx_status_t Unmount();
    virtual ~Blobstore();

    // Returns the root blob
    zx_status_t GetRootBlob(fbl::RefPtr<VnodeBlob>* out);

    // Searches for a blob by name.
    // - If a readable blob with the same name exists, return it.
    // - If a blob with the same name exists, but it is not readable,
    //   ZX_ERR_BAD_STATE is returned.
    //
    // 'out' may be null -- the same error code will be returned as if it
    // was a valid pointer.
    //
    // If 'out' is not null, then the blob's  will be added to the
    // "quick lookup" map if it was not there already.
    zx_status_t LookupBlob(const Digest& digest, fbl::RefPtr<VnodeBlob>* out);

    // Creates a new blob in-memory, with no backing disk storage (yet).
    // If a blob with the name already exists, this function fails.
    //
    // Adds Blob to the "quick lookup" map.
    zx_status_t NewBlob(const Digest& digest, fbl::RefPtr<VnodeBlob>* out);

    // Removes blob from 'active' hashmap.
    zx_status_t ReleaseBlob(VnodeBlob* blob);

    zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len);

    zx_status_t AttachVmo(zx_handle_t vmo, vmoid_t* out);
    zx_status_t Txn(block_fifo_request_t* requests, size_t count) {
        return block_fifo_txn(fifo_client_, requests, count);
    }
    txnid_t TxnId() const { return txnid_; }

    // If possible, attempt to resize the blobstore partition.
    // Add one additional slice for inodes.
    zx_status_t AddInodes();
    // Add enough slices required to hold nblocks additional blocks.
    zx_status_t AddBlocks(size_t nblocks);

    int blockfd_;
    blobstore_info_t info_;

private:
    friend class BlobstoreChecker;

    Blobstore(int fd, const blobstore_info_t* info);
    zx_status_t LoadBitmaps();

    // Finds space for a block in memory. Does not update disk.
    zx_status_t AllocateBlocks(size_t nblocks, size_t* blkno_out);
    void FreeBlocks(size_t nblocks, size_t blkno);

    // Finds space for a blob node in memory. Does not update disk.
    zx_status_t AllocateNode(size_t* node_index_out);
    void FreeNode(size_t node_index);

    // Access the nth inode of the node map
    blobstore_inode_t* GetNode(size_t index) const;

    // Given a contiguous number of blocks after a starting block,
    // write out the bitmap to disk for the corresponding blocks.
    zx_status_t WriteBitmap(WriteTxn* txn, uint64_t nblocks, uint64_t start_block);

    // Given a node within the node map at an index, write it to disk.
    zx_status_t WriteNode(WriteTxn* txn, size_t map_index);

    // Enqueues an update for allocated inode/block counts
    zx_status_t CountUpdate(WriteTxn* txn);

    // VnodeBlobs exist in the WAVLTree as long as one or more reference exists;
    // when the Vnode is deleted, it is immediately removed from the WAVL tree.
    using WAVLTreeByMerkle = fbl::WAVLTree<const uint8_t*,
                                            VnodeBlob*,
                                            MerkleRootTraits,
                                            VnodeBlob::TypeWavlTraits>;
    WAVLTreeByMerkle hash_{}; // Map of all 'in use' blobs

    fifo_client_t* fifo_client_{};
    txnid_t txnid_{};
    RawBitmap block_map_{};
    vmoid_t block_map_vmoid_{};
    fbl::unique_ptr<MappedVmo> node_map_{};
    vmoid_t node_map_vmoid_{};
    fbl::unique_ptr<MappedVmo> info_vmo_{};
    vmoid_t info_vmoid_{};
};

class BlobstoreChecker {
public:
    BlobstoreChecker();
    void Init(fbl::RefPtr<Blobstore> vnode);
    void TraverseInodeBitmap();
    void TraverseBlockBitmap();
    zx_status_t CheckAllocatedCounts() const;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(BlobstoreChecker);
    fbl::RefPtr<Blobstore> blobstore_;
    uint32_t alloc_inodes_;
    uint32_t alloc_blocks_;
};

#endif

int blobstore_mkfs(int fd, uint64_t block_count);

// Exclusively host-side functionality
#ifndef __Fuchsia__
int blobstore_add_blob(int fd, int data_fd);
#endif

zx_status_t blobstore_mount(fbl::RefPtr<VnodeBlob>* out, int blockfd);
zx_status_t blobstore_create(fbl::RefPtr<Blobstore>* out, int blockfd);
zx_status_t blobstore_check(fbl::RefPtr<Blobstore> vnode);

zx_status_t readblk(int fd, uint64_t bno, void* data);
zx_status_t writeblk(int fd, uint64_t bno, const void* data);

uint64_t MerkleTreeBlocks(const blobstore_inode_t& blobNode);
// Get a pointer to the nth block of the bitmap.
inline void* get_raw_bitmap_data(const RawBitmap& bm, uint64_t n) {
    assert(n * kBlobstoreBlockSize < bm.size());                  // Accessing beyond end of bitmap
    assert(kBlobstoreBlockSize <= (n + 1) * kBlobstoreBlockSize); // Avoid overflow
    return fs::GetBlock<kBlobstoreBlockSize>(bm.StorageUnsafe()->GetData(), n);
}
zx_status_t blobstore_check_info(const blobstore_info_t* info, uint64_t max);
zx_status_t blobstore_get_blockcount(int fd, uint64_t* out);

} // namespace blobstore
