// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the global Blobfs structure used for constructing a Blobfs filesystem in
// memory.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <string.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/rle-bitmap.h>
#include <block-client/cpp/client.h>
#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fs/block-txn.h>
#include <fs/managed-vfs.h>
#include <fs/ticker.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>
#include <trace/event.h>

#include <blobfs/allocator.h>
#include <blobfs/common.h>
#include <blobfs/extent-reserver.h>
#include <blobfs/format.h>
#include <blobfs/iterator/allocated-extent-iterator.h>
#include <blobfs/iterator/extent-iterator.h>
#include <blobfs/journal.h>
#include <blobfs/lz4.h>
#include <blobfs/metrics.h>
#include <blobfs/node-reserver.h>
#include <blobfs/vnode.h>
#include <blobfs/writeback.h>

#include <atomic>
#include <utility>

namespace blobfs {

class Blobfs;
class Journal;
class VnodeBlob;
class WritebackQueue;
class WritebackWork;

using digest::Digest;

enum class EnqueueType {
    kJournal,
    kData,
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

// CachePolicy describes the techniques used to cache blobs in memory, avoiding
// re-reading and re-verifying them from disk.
enum class CachePolicy {
    // When all references to a blob are closed, the blob is evicted from
    // memory. On re-acquisition, the blob is read from disk and re-verified.
    //
    // This option avoids using memory for any longer than it needs to, but
    // may result in higher performance penalties for blobfs that are frequently
    // opened and closed.
    EvictImmediately,

    // The blob is never evicted from memory, unless it has been fully deleted
    // and there are no additional references.
    //
    // This option costs a significant amount of memory, but it results in high
    // performance.
    NeverEvict,
};

// Toggles that may be set on blobfs during initialization.
struct MountOptions {
    bool readonly = false;
    bool metrics = false;
    bool journal = false;
    CachePolicy cache_policy = CachePolicy::EvictImmediately;
};

class Blobfs : public fs::ManagedVfs, public fbl::RefCounted<Blobfs>,
               public fs::TransactionHandler, public SpaceManager {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Blobfs);
    friend class VnodeBlob;

    ////////////////
    // fs::ManagedVfs interface.

    void Shutdown(fs::Vfs::ShutdownCallback closure) final;

    ////////////////
    // fs::TransactionHandler interface.

    uint32_t FsBlockSize() const final {
        return kBlobfsBlockSize;
    }

    uint32_t DeviceBlockSize() const final {
        return block_info_.block_size;
    }

    groupid_t BlockGroupID() final {
        thread_local groupid_t group_ = next_group_.fetch_add(1);
        ZX_ASSERT_MSG(group_ < MAX_TXN_GROUP_COUNT, "Too many threads accessing block device");
        return group_;
    }

    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
        TRACE_DURATION("blobfs", "Blobfs::Transaction", "count", count);
        return fifo_client_.Transaction(requests, count);
    }

    ////////////////
    // SpaceManager interface.

    zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final;
    zx_status_t DetachVmo(vmoid_t vmoid) final;
    zx_status_t AddInodes(fzl::ResizeableVmoMapper* node_map) final;
    zx_status_t AddBlocks(size_t nblocks, RawBitmap* block_map) final;


    ////////////////
    // Other methods.

    uint64_t DataStart() const {
        return DataStartBlock(info_);
    }

    bool CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                              uint64_t* first_unset = nullptr) const {
        return allocator_->CheckBlocksAllocated(start_block, end_block, first_unset);
    }
    AllocatedExtentIterator GetExtents(uint32_t node_index) {
        return AllocatedExtentIterator(allocator_.get(), node_index);
    }

    Inode* GetNode(uint32_t node_index) {
        return allocator_->GetNode(node_index);
    }
    zx_status_t ReserveBlocks(size_t num_blocks, fbl::Vector<ReservedExtent>* out_extents) {
        return allocator_->ReserveBlocks(num_blocks, out_extents);
    }
    zx_status_t ReserveNodes(size_t num_nodes, fbl::Vector<ReservedNode>* out_node) {
        return allocator_->ReserveNodes(num_nodes, out_node);
    }

    static zx_status_t Create(fbl::unique_fd blockfd, const MountOptions& options,
                              const Superblock* info, fbl::unique_ptr<Blobfs>* out);

    void SetCachePolicy(CachePolicy policy) { cache_policy_ = policy; }
    void CollectMetrics() { collecting_metrics_ = true; }
    bool CollectingMetrics() const { return collecting_metrics_; }
    void DisableMetrics() { collecting_metrics_ = false; }
    void DumpMetrics() const {
        if (collecting_metrics_) {
            metrics_.Dump();
        }
    }

    void SetUnmountCallback(fbl::Closure closure) {
        on_unmount_ = std::move(closure);
    }

    // Initializes the WritebackQueue and Journal (if enabled in |options|),
    // replaying any existing journal entries.
    zx_status_t InitializeWriteback(const MountOptions& options);

    // Returns the capacity of the writeback buffer in blocks.
    size_t WritebackCapacity() const;

    virtual ~Blobfs();

    // Invokes "open" on the root directory.
    // Acts as a special-case to bootstrap filesystem mounting.
    zx_status_t OpenRootNode(fbl::RefPtr<VnodeBlob>* out);

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

    // Removes blob from 'active' hashmap and deletes all metadata associated with it.
    zx_status_t PurgeBlob(VnodeBlob* blob);

    zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual);

    int Fd() const {
        return blockfd_.get();
    }

    const Superblock& Info() const { return info_; }

    // Returns an unique identifier for this instance.
    uint64_t GetFsId() const { return fs_id_; }

    using SyncCallback = fs::Vnode::SyncCallback;
    void Sync(SyncCallback closure);

    // Updates aggregate information about the total number of created
    // blobs since mounting.
    void UpdateAllocationMetrics(uint64_t size_data, const fs::Duration& duration);

    // Updates aggregate information about the number of blobs opened
    // since mounting.
    void UpdateLookupMetrics(uint64_t size);

    // Updates aggregates information about blobs being written back
    // to blobfs since mounting.
    void UpdateClientWriteMetrics(uint64_t data_size, uint64_t merkle_size,
                                  const fs::Duration& enqueue_duration,
                                  const fs::Duration& generate_duration);

    // Updates aggregate information about flushing bits down
    // to the underlying storage driver.
    void UpdateWritebackMetrics(uint64_t size, const fs::Duration& duration);

    // Updates aggregate information about reading blobs from storage
    // since mounting.
    void UpdateMerkleDiskReadMetrics(uint64_t size, const fs::Duration& duration);

    // Updates aggregate information about decompressing blobs from storage
    // since mounting.
    void UpdateMerkleDecompressMetrics(uint64_t size_compressed, uint64_t size_uncompressed,
                                       const fs::Duration& read_duration,
                                       const fs::Duration& decompress_duration);

    // Updates aggregate information about general verification info
    // since mounting.
    void UpdateMerkleVerifyMetrics(uint64_t size_data, uint64_t size_merkle,
                                   const fs::Duration& duration);

    zx_status_t CreateWork(fbl::unique_ptr<WritebackWork>* out, VnodeBlob* vnode);

    // Enqueues |work| to the appropriate buffer. If |journal| is true and the journal is enabled,
    // the transaction(s) will first be written to the journal. Otherwise, they will be sent
    // straight to the writeback buffer.
    zx_status_t EnqueueWork(fbl::unique_ptr<WritebackWork> work, EnqueueType type)
        __WARN_UNUSED_RESULT;

    // Does a single pass of all blobs, creating uninitialized Vnode
    // objects for them all.
    //
    // By executing this function at mount, we can quickly assert
    // either the presence or absence of a blob on the system without
    // further scanning.
    zx_status_t InitializeVnodes() __TA_EXCLUDES(hash_lock_);

    // Remove the Vnode without storing it in the closed Vnode cache. This
    // function should be used when purging a blob, as it will prevent
    // additional lookups of VnodeBlob from being made.
    //
    // Precondition: The blob must exist in |open_hash_|.
    void VnodeReleaseHard(VnodeBlob* vn) __TA_EXCLUDES(hash_lock_);

    // Resurrect a Vnode with no strong references, and relocate
    // it from |open_hash_| into |closed_hash_|.
    //
    // Precondition: The blob must exist in the |open_hash_| with
    // no strong references.
    void VnodeReleaseSoft(VnodeBlob* vn) __TA_EXCLUDES(hash_lock_);

private:
    friend class BlobfsChecker;

    Blobfs(fbl::unique_fd fd, const Superblock* info);

    // Reloads metadata from disk. Useful when metadata on disk
    // may have changed due to journal playback.
    zx_status_t Reload();

    // Inserts a Vnode into the |closed_hash_|, tears down
    // cache Vnode state, and leaks a reference to the Vnode
    // if it was added to the cache successfully.
    //
    // This prevents the vnode from ever being torn down, unless
    // it is re-acquired from |closed_hash_| and released manually
    // (with an identifier to not relocate the Vnode into the cache).
    //
    // Returns an error if the Vnode already exists in the cache.
    zx_status_t VnodeInsertClosedLocked(fbl::RefPtr<VnodeBlob> vn) __TA_REQUIRES(hash_lock_);

    // Upgrades a Vnode which exists in the |closed_hash_| into |open_hash_|,
    // and acquire the strong reference the Vnode which was leaked by
    // |VnodeInsertClosedLocked()|, if it exists.
    //
    // Precondition: The Vnode must not exist in |open_hash_|.
    fbl::RefPtr<VnodeBlob> VnodeUpgradeLocked(const uint8_t* key) __TA_REQUIRES(hash_lock_);

    // Adds reserved blocks to allocated bitmap and writes the bitmap out to disk.
    void PersistBlocks(WritebackWork* wb, const ReservedExtent& extent);

    // Frees blocks from the allocated map (if allocated) and updates disk if necessary.
    void FreeExtent(WritebackWork* wb, const Extent& extent);

    // Free a single node. Doesn't attempt to parse the type / traverse nodes;
    // this function just deletes a single node.
    void FreeNode(WritebackWork* wb, uint32_t node_index);

    // Frees an inode, from both the reserved map and the inode table. If the
    // inode was allocated in the inode table, write the deleted inode out to
    // disk.
    void FreeInode(WritebackWork* wb, uint32_t node_index);

    // Writes node data to the inode table and updates disk.
    void PersistNode(WritebackWork* wb, uint32_t node_index);

    // Given a contiguous number of blocks after a starting block,
    // write out the bitmap to disk for the corresponding blocks.
    // Should only be called by PersistBlocks and FreeExtent.
    void WriteBitmap(WritebackWork* wb, uint64_t nblocks, uint64_t start_block);

    // Given a node within the node map at an index, write it to disk.
    // Should only be called by AllocateNode and FreeNode.
    void WriteNode(WritebackWork* wb, uint32_t map_index);

    // Enqueues an update for allocated inode/block counts.
    void WriteInfo(WritebackWork* wb);

    // Creates an unique identifier for this instance. This is to be called only during
    // "construction".
    zx_status_t CreateFsId();

    // Verifies that the contents of a blob are valid.
    zx_status_t VerifyBlob(uint32_t node_index);

    // VnodeBlobs exist in the WAVLTree as long as one or more reference exists;
    // when the Vnode is deleted, it is immediately removed from the WAVL tree.
    using WAVLTreeByMerkle = fbl::WAVLTree<const uint8_t*,
                                           VnodeBlob*,
                                           MerkleRootTraits,
                                           VnodeBlob::TypeWavlTraits>;
    fbl::unique_ptr<WritebackQueue> writeback_;
    fbl::unique_ptr<Journal> journal_;
    Superblock info_;

    fbl::Mutex hash_lock_;
    WAVLTreeByMerkle open_hash_ __TA_GUARDED(hash_lock_){};   // All 'in use' blobs.
    WAVLTreeByMerkle closed_hash_ __TA_GUARDED(hash_lock_){}; // All 'closed' blobs.

    fbl::unique_fd blockfd_;
    block_info_t block_info_ = {};
    std::atomic<groupid_t> next_group_ = {};
    block_client::Client fifo_client_;

    fbl::unique_ptr<Allocator> allocator_;

    fzl::ResizeableVmoMapper info_mapping_;
    vmoid_t info_vmoid_ = {};

    uint64_t fs_id_ = 0;

    bool collecting_metrics_ = false;
    BlobfsMetrics metrics_ = {};

    CachePolicy cache_policy_;
    fbl::Closure on_unmount_ = {};
};

zx_status_t Initialize(fbl::unique_fd blockfd, const MountOptions& options,
                       fbl::unique_ptr<Blobfs>* out);
zx_status_t Mount(async_dispatcher_t* dispatcher, fbl::unique_fd blockfd,
                  const MountOptions& options, zx::channel root,
                  fbl::Closure on_unmount);

} // namespace blobfs
