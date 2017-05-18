// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "blobstore.h"

#include <bitmap/raw-bitmap.h>
#include <merkle/digest.h>
#include <mx/event.h>
#include <mx/vmo.h>
#include <mxtl/algorithm.h>
#include <mxtl/macros.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

#include <fs/mapped-vmo.h>
#include <fs/vfs.h>

namespace blobstore {

class Blobstore;
class VnodeBlob;

typedef uint32_t BlobFlags;

// After Open;
constexpr BlobFlags kBlobStateEmpty       = 0x00010000; // Not yet allocated
// After Ioctl configuring size:
constexpr BlobFlags kBlobStateDataWrite   = 0x00020000; // Data is being written
// After Writing:
constexpr BlobFlags kBlobStateReadable    = 0x00040000; // Readable
// After Unlink:
constexpr BlobFlags kBlobStateReleasing   = 0x00080000; // In the process of unlinking
// Unrecoverable error state:
constexpr BlobFlags kBlobStateError       = 0x00100000; // Unrecoverable error state
constexpr BlobFlags kBlobStateMask        = 0x00FF0000;

// Informational non-state flags:
constexpr BlobFlags kBlobFlagSync         = 0x01000000; // The blob is being written to disk
constexpr BlobFlags kBlobFlagDeletable    = 0x02000000; // This node should be unlinked when closed
constexpr BlobFlags kBlobFlagDirectory    = 0x04000000; // This node represents the root directory
constexpr BlobFlags kBlobOtherMask        = 0xFF000000;

static_assert(((kBlobStateMask | kBlobOtherMask) & V_FLAG_RESERVED_MASK) == 0,
              "Blobstore flags conflict with VFS-reserved flags");

class VnodeBlob final : public fs::Vnode {
public:
    // Intrusive methods and structures
    using WAVLTreeNodeState = mxtl::WAVLTreeNodeState<VnodeBlob*>;
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

    ssize_t Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                  void* out_buf, size_t out_len) final;

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
    VnodeBlob(mxtl::RefPtr<Blobstore> bs);
    // Constructs actual blobs
    VnodeBlob(mxtl::RefPtr<Blobstore> bs, const merkle::Digest& digest);
    virtual ~VnodeBlob();

private:
    friend struct TypeWavlTraits;

    DISALLOW_COPY_ASSIGN_AND_MOVE(VnodeBlob);

    void BlobCloseHandles();

    // Returns a handle to an event which will be signalled when
    // the blob is readable.
    //
    // Returns "NO_ERROR" if blob is already readable.
    // Otherwise, returns size of the handle.
    mx_status_t GetReadableEvent(mx_handle_t* out);

    mx_status_t CopyVmo(mx_rights_t rights, mx_handle_t* out);

    void QueueUnlink();

    // If successful, allocates Blob Node and Blocks (in-memory)
    // kBlobStateEmpty --> kBlobStateDataWrite
    mx_status_t SpaceAllocate(uint64_t size_data);

    // Writes to either the Merkle Tree or the Data section,
    // depending on the state.
    mx_status_t WriteInternal(const void* data, size_t len, size_t* actual);

    // Reads from a blob.
    // Requires: kBlobStateReadable
    mx_status_t ReadInternal(void* data, size_t len, size_t off, size_t* actual);

    // Vnode I/O operations
    mx_status_t GetHandles(uint32_t flags, mx_handle_t* hnds,
                           uint32_t* type, void* extra, uint32_t* esize) final;
    mx_status_t Open(uint32_t flags) final;
    fs::Dispatcher* GetDispatcher() final;
    mx_status_t Readdir(void* cookie, void* dirents, size_t len) final;
    ssize_t Read(void* data, size_t len, size_t off) final;
    ssize_t Write(const void* data, size_t len, size_t off) final;
    mx_status_t Lookup(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len) final;
    mx_status_t Getattr(vnattr_t* a) final;
    mx_status_t Create(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len,
                       uint32_t mode) final;
    mx_status_t Truncate(size_t len) final;
    mx_status_t Unlink(const char* name, size_t len, bool must_be_dir) final;
    mx_status_t Mmap(int flags, size_t len, size_t* off, mx_handle_t* out) final;
    mx_status_t Sync() final;

    // Read both VMOs into memory, if we haven't already.
    //
    // TODO(smklein): When we have can register the Blob Store as a pager
    // service, and it can properly handle pages faults on a vnode's contents,
    // then we can avoid reading the entire blob up-front. Until then, read
    // the contents of a VMO into memory when it is opened.
    mx_status_t InitVmos();

    mx_status_t WriteShared(size_t start, size_t len, uint64_t maxlen,
                            mx_handle_t vmo, uint64_t start_block);
    // Called by Blob once the last write has completed, updating the
    // on-disk metadata.
    mx_status_t WriteMetadata();

    WAVLTreeNodeState type_wavl_state_;

    const mxtl::RefPtr<Blobstore> blobstore_;
    mxtl::unique_ptr<MappedVmo> merkle_tree_;
    mxtl::unique_ptr<MappedVmo> blob_;

    mx::event readable_event_;
    uint64_t bytes_written_;

    BlobFlags flags_;
    uint8_t digest_[merkle::Digest::kLength];

    size_t map_index_;
};

// We need to define this structure to allow the Blob to be indexable by a key
// which is larger than a primitive type: the keys are 'merkle::Digest::kLength'
// bytes long.
struct MerkleRootTraits {
    static const uint8_t* GetKey(const VnodeBlob& obj) { return obj.GetKey(); }
    static bool LessThan(const uint8_t* k1, const uint8_t* k2) {
        return memcmp(k1, k2, merkle::Digest::kLength) < 0;
    }
    static bool EqualTo(const uint8_t* k1, const uint8_t* k2) {
        return memcmp(k1, k2, merkle::Digest::kLength) == 0;
    }
};

class Blobstore : public mxtl::RefCounted<Blobstore> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Blobstore);
    friend class VnodeBlob;

    static mx_status_t Create(int blockfd, const blobstore_info_t* info, mxtl::RefPtr<VnodeBlob>* out);
    mx_status_t Unmount();
    virtual ~Blobstore();

    // Searches for a blob by name.
    // - If a readable blob with the same name exists, return it.
    // - If a blob with the same name exists, but it is not readable,
    //   ERR_BAD_STATE is returned.
    //
    // 'out' may be null -- the same error code will be returned as if it
    // was a valid pointer.
    //
    // If 'out' is not null, then the blob's  will be added to the
    // "quick lookup" map if it was not there already.
    mx_status_t LookupBlob(const merkle::Digest& digest, mxtl::RefPtr<VnodeBlob>* out);

    // Creates a new blob in-memory, with no backing disk storage (yet).
    // If a blob with the name already exists, this function fails.
    //
    // Adds Blob to the "quick lookup" map.
    mx_status_t NewBlob(const merkle::Digest& digest, mxtl::RefPtr<VnodeBlob>* out);

    // Removes blob from 'active' hashmap.
    mx_status_t ReleaseBlob(VnodeBlob* blob);

    mx_status_t Readdir(void* cookie, void* dirents, size_t len);

    int blockfd_;
    blobstore_info_t info_;
private:
    Blobstore(int fd, const blobstore_info_t* info);
    mx_status_t LoadBitmaps();

    // Finds space for a block in memory. Does not update disk.
    mx_status_t AllocateBlocks(size_t nblocks, size_t* blkno_out);
    void FreeBlocks(size_t nblocks, size_t blkno);

    // Finds space for a blob node in memory. Does not update disk.
    mx_status_t AllocateNode(size_t* node_index_out);
    void FreeNode(size_t node_index);

    // Access the nth block of the block bitmap.
    void* GetBlockmapData(uint64_t n) const;
    // Access the nth block of the node map.
    void* GetNodemapData(uint64_t n) const;

    // Given a contiguous number of blocks after a starting block,
    // write out the bitmap to disk for the corresponding blocks.
    mx_status_t WriteBitmap(uint64_t nblocks, uint64_t start_block);

    // Given a node within the node map at an index, write it to disk.
    mx_status_t WriteNode(size_t map_index);

    // VnodeBlobs exist in the WAVLTree as long as one or more reference exists;
    // when the Vnode is deleted, it is immediately removed from the WAVL tree.
    using WAVLTreeByMerkle = mxtl::WAVLTree<const uint8_t*,
                                            VnodeBlob*,
                                            MerkleRootTraits,
                                            VnodeBlob::TypeWavlTraits>;
    WAVLTreeByMerkle hash_; // Map of all 'in use' blobs

    RawBitmap block_map_;
    mxtl::unique_ptr<blobstore_inode_t[]> node_map_;
};

int blobstore_mkfs(int fd);

mx_status_t blobstore_mount(mxtl::RefPtr<VnodeBlob>* out, int blockfd);

mx_status_t readblk(int fd, uint64_t bno, void* data);
mx_status_t writeblk(int fd, uint64_t bno, const void* data);

} // namespace blobstore
