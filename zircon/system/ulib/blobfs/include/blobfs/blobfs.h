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
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fs/block-txn.h>
#include <fs/managed-vfs.h>
#include <fs/metrics.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <fuchsia/blobfs/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fzl/fdio.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>
#include <trace/event.h>

#include <blobfs/allocator.h>
#include <blobfs/blob-cache.h>
#include <blobfs/blob.h>
#include <blobfs/block-device.h>
#include <blobfs/common.h>
#include <blobfs/directory.h>
#include <blobfs/extent-reserver.h>
#include <blobfs/format.h>
#include <blobfs/iterator/allocated-extent-iterator.h>
#include <blobfs/iterator/extent-iterator.h>
#include <blobfs/journal.h>
#include <blobfs/metrics.h>
#include <blobfs/node-reserver.h>
#include <blobfs/writeback.h>

#include <atomic>
#include <utility>

namespace blobfs {

using digest::Digest;

enum class Writability {
    // Do not write to persistent storage under any circumstances whatsoever.
    ReadOnlyDisk,
    // Do not allow users of the filesystem to mutate filesystem state. This
    // state allows the journal to replay while initializing writeback.
    ReadOnlyFilesystem,
    // Permit all operations.
    Writable,
};

// Toggles that may be set on blobfs during initialization.
struct MountOptions {
    Writability writability = Writability::Writable;
    bool metrics = false;
    bool journal = false;
    CachePolicy cache_policy = CachePolicy::EvictImmediately;
};

class Blobfs : public fs::ManagedVfs,
               public fbl::RefCounted<Blobfs>,
               public TransactionManager {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Blobfs);

    ////////////////
    // fs::ManagedVfs interface.

    void Shutdown(fs::Vfs::ShutdownCallback closure) final;

    ////////////////
    // TransactionManager's fs::TransactionHandler interface.
    //
    // Allows transmitting read and write transactions directly to the underlying storage.

    uint32_t FsBlockSize() const final { return kBlobfsBlockSize; }

    uint32_t DeviceBlockSize() const final { return block_info_.block_size; }

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
    // TransactionManager's SpaceManager interface.
    //
    // Allows viewing and controlling the size of the underlying volume.

    const Superblock& Info() const final { return info_; }
    zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final;
    zx_status_t DetachVmo(vmoid_t vmoid) final;
    zx_status_t AddInodes(fzl::ResizeableVmoMapper* node_map) final;
    zx_status_t AddBlocks(size_t nblocks, RawBitmap* block_map) final;

    ////////////////
    // TransactionManager interface.
    //
    // Allows attaching VMOs, controlling the underlying volume, and sending transactions to the
    // underlying storage (optionally through the journal).

    BlobfsMetrics& LocalMetrics() final {
        return metrics_;
    }
    size_t WritebackCapacity() const final;
    zx_status_t CreateWork(fbl::unique_ptr<WritebackWork>* out, Blob* vnode) final;
    zx_status_t EnqueueWork(fbl::unique_ptr<WritebackWork> work, EnqueueType type) final;

    ////////////////
    // Other methods.

    uint64_t DataStart() const { return DataStartBlock(info_); }

    bool CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                              uint64_t* first_unset = nullptr) const {
        return allocator_->CheckBlocksAllocated(start_block, end_block, first_unset);
    }
    AllocatedExtentIterator GetExtents(uint32_t node_index) {
        return AllocatedExtentIterator(allocator_.get(), node_index);
    }

    Allocator* GetAllocator() { return allocator_.get(); }

    Inode* GetNode(uint32_t node_index) { return allocator_->GetNode(node_index); }
    zx_status_t ReserveBlocks(size_t num_blocks, fbl::Vector<ReservedExtent>* out_extents) {
        return allocator_->ReserveBlocks(num_blocks, out_extents);
    }
    zx_status_t ReserveNodes(size_t num_nodes, fbl::Vector<ReservedNode>* out_node) {
        return allocator_->ReserveNodes(num_nodes, out_node);
    }

    static zx_status_t Create(std::unique_ptr<BlockDevice> device, const MountOptions& options,
                              const Superblock* info, fbl::unique_ptr<Blobfs>* out);

    void CollectMetrics() {
        collecting_metrics_ = true;
        cobalt_metrics_.EnableMetrics(true);
    }
    bool CollectingMetrics() const { return cobalt_metrics_.IsEnabled(); }
    void DisableMetrics() {
        cobalt_metrics_.EnableMetrics(false);
        collecting_metrics_ = false;
    }
    void DumpMetrics() const {
        if (collecting_metrics_) {
            metrics_.Dump();
        }
    }

    void SetUnmountCallback(fbl::Closure closure) { on_unmount_ = std::move(closure); }

    // Initializes the WritebackQueue and Journal, replaying any existing journal entries
    // if requested.
    //
    // If the underlying block device is read-only, the journal may not be
    // replayed, and this function returns ZX_ERR_ACCESS_DENIED.
    // If the filesystem is to be mounted read-only or read + write, the journal may be replayed.
    zx_status_t InitializeWriteback(Writability writability, bool journal_enabled);

    virtual ~Blobfs();

    // Invokes "open" on the root directory.
    // Acts as a special-case to bootstrap filesystem mounting.
    zx_status_t OpenRootNode(fbl::RefPtr<Directory>* out);

    BlobCache& Cache() {
        return blob_cache_;
    }

    zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual);

    BlockDevice* Device() const {
        return block_device_.get();
    }

    // Returns an unique identifier for this instance.
    uint64_t GetFsId() const { return fs_id_; }

    using SyncCallback = fs::Vnode::SyncCallback;
    void Sync(SyncCallback closure);

    // Frees an inode, from both the reserved map and the inode table. If the
    // inode was allocated in the inode table, write the deleted inode out to
    // disk.
    void FreeInode(WritebackWork* wb, uint32_t node_index);

    // Does a single pass of all blobs, creating uninitialized Vnode
    // objects for them all.
    //
    // By executing this function at mount, we can quickly assert
    // either the presence or absence of a blob on the system without
    // further scanning.
    zx_status_t InitializeVnodes();

    // Writes node data to the inode table and updates disk.
    void PersistNode(WritebackWork* wb, uint32_t node_index);

    // Adds reserved blocks to allocated bitmap and writes the bitmap out to disk.
    void PersistBlocks(WritebackWork* wb, const ReservedExtent& extent);

    fs::VnodeMetrics* GetMutableVnodeMetrics() { return cobalt_metrics_.mutable_vnode_metrics(); }

    // Record the location and size of all non-free block regions.
    fbl::Vector<BlockRegion> GetAllocatedRegions() const {
        return allocator_->GetAllocatedRegions();
    }
private:
    friend class BlobfsChecker;

    Blobfs(std::unique_ptr<BlockDevice> device, const Superblock* info);

    // Reloads metadata from disk. Useful when metadata on disk
    // may have changed due to journal playback.
    zx_status_t Reload();

    // Frees blocks from the allocated map (if allocated) and updates disk if necessary.
    void FreeExtent(WritebackWork* wb, const Extent& extent);

    // Free a single node. Doesn't attempt to parse the type / traverse nodes;
    // this function just deletes a single node.
    void FreeNode(WritebackWork* wb, uint32_t node_index);

    // Given a contiguous number of blocks after a starting block,
    // write out the bitmap to disk for the corresponding blocks.
    // Should only be called by PersistBlocks and FreeExtent.
    void WriteBitmap(WritebackWork* wb, uint64_t nblocks, uint64_t start_block);

    // Given a node within the node map at an index, write it to disk.
    // Should only be called by AllocateNode and FreeNode.
    void WriteNode(WritebackWork* wb, uint32_t map_index);

    // Enqueues an update for allocated inode/block counts.
    void WriteInfo(WritebackWork* wb);

    // When will flush the metrics in the calling thread and will schedule itself
    // to flush again in the future.
    void ScheduleMetricFlush();

    // Creates an unique identifier for this instance. This is to be called only during
    // "construction".
    zx_status_t CreateFsId();

    // Verifies that the contents of a blob are valid.
    zx_status_t VerifyBlob(uint32_t node_index);

    fbl::unique_ptr<WritebackQueue> writeback_;
    fbl::unique_ptr<Journal> journal_;
    Superblock info_;

    BlobCache blob_cache_;

    std::unique_ptr<BlockDevice> block_device_;
    fuchsia_hardware_block_BlockInfo block_info_ = {};
    std::atomic<groupid_t> next_group_ = {};
    block_client::Client fifo_client_;

    fbl::unique_ptr<Allocator> allocator_;

    fzl::ResizeableVmoMapper info_mapping_;
    vmoid_t info_vmoid_ = {};

    uint64_t fs_id_ = 0;

    bool collecting_metrics_ = false;
    BlobfsMetrics metrics_ = {};

    fbl::Closure on_unmount_ = {};

    // TODO(gevalentino): clean up old metrics and update this to inspect API.
    fs::Metrics cobalt_metrics_;
    async::Loop flush_loop_ = async::Loop(&kAsyncLoopConfigNoAttachToThread);
};

zx_status_t Initialize(fbl::unique_fd blockfd, const MountOptions& options,
                       fbl::unique_ptr<Blobfs>* out);
zx_status_t Mount(async_dispatcher_t* dispatcher, fbl::unique_fd blockfd,
                  const MountOptions& options, zx::channel root, fbl::Closure on_unmount);

} // namespace blobfs
