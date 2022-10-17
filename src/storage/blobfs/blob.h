// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains Vnodes which back a Blobfs filesystem.

#ifndef SRC_STORAGE_BLOBFS_BLOB_H_
#define SRC_STORAGE_BLOBFS_BLOB_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/cpp/wait.h>
#include <lib/fpromise/promise.h>
#include <lib/zx/event.h>
#include <string.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <storage/buffer/owned_vmoid.h>

#include "src/lib/digest/digest.h"
#include "src/lib/storage/vfs/cpp/journal/data_streamer.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vnode.h"
#include "src/storage/blobfs/allocator/allocator.h"
#include "src/storage/blobfs/allocator/extent_reserver.h"
#include "src/storage/blobfs/allocator/node_reserver.h"
#include "src/storage/blobfs/blob_cache.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/compression/blob_compressor.h"
#include "src/storage/blobfs/compression/compressor.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/format_assertions.h"
#include "src/storage/blobfs/loader_info.h"
#include "src/storage/blobfs/transaction.h"

namespace blobfs {

class Blobfs;
class BlobDataProducer;

enum class BlobState : uint8_t {
  kEmpty,      // After open.
  kDataWrite,  // After space reserved (but allocation not yet persisted).
  kReadable,   // After writing.
  kPurged,     // After unlink,
  kError,      // Unrecoverable error state.
};

class Blob final : public CacheNode, fbl::Recyclable<Blob> {
 public:
  // Constructs a blob, reads in data, verifies the contents, then destroys the in-memory copy.
  static zx_status_t LoadAndVerifyBlob(Blobfs* bs, uint32_t node_index);

  Blob(Blobfs* bs, const digest::Digest& digest, CompressionAlgorithm data_format);

  // Creates a readable blob from existing data.
  Blob(Blobfs* bs, uint32_t node_index, const Inode& inode);

  ~Blob() override;

  // Note this Blob's hash is stored on the CacheNode base class:
  //
  // const digest::Digest& digest() const;

  // fs::Vnode implementation:
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final __TA_EXCLUDES(mutex_);
  fs::VnodeProtocolSet GetProtocols() const final __TA_EXCLUDES(mutex_);
  bool ValidateRights(fs::Rights rights) const final __TA_EXCLUDES(mutex_);
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final
      __TA_EXCLUDES(mutex_);
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) final
      __TA_EXCLUDES(mutex_);
  zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) final
      __TA_EXCLUDES(mutex_);
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final __TA_EXCLUDES(mutex_);
  zx_status_t Truncate(size_t len) final __TA_EXCLUDES(mutex_);
  zx::result<std::string> GetDevicePath() const final __TA_EXCLUDES(mutex_);
  zx_status_t GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo* out_vmo) final
      __TA_EXCLUDES(mutex_);
  void Sync(SyncCallback on_complete) final __TA_EXCLUDES(mutex_);

  // fs::PagedVnode implementation.
  void VmoRead(uint64_t offset, uint64_t length) override __TA_EXCLUDES(mutex_);

  // Required for memory management, see the class comment above Vnode for more.
  void fbl_recycle() { RecycleNode(); }

  // Returned true if this blob is marked for deletion.
  //
  // This is called outside the lock and so the deletion state might change out from under the
  // caller. It should not be used by anything requiring exact correctness. Currently this is used
  // only to skip verifying deleted blobs which can tolerate mistakenly checking deleted blobs
  // sometimes.
  bool DeletionQueued() const __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return deletable_;
  }

  // Returns a unique identifier for this blob
  //
  // This is called outside the lock which means there can be a race for the inode to be assigned.
  // The inode number changes for newly created blobs from 0 to a nonzero number when we start to
  // write to it. For blobs just read from disk (most of them) the inode number won't change.
  uint32_t Ino() const __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return map_index_;
  }

  // Size of the blob data (i.e. total number of bytes that can be read).
  uint64_t FileSize() const __TA_EXCLUDES(mutex_);

  void CompleteSync() __TA_EXCLUDES(mutex_);

  // Notification that the associated Blobfs is tearing down. This will clean up any extra
  // references such that the Blob can be deleted.
  void WillTeardownFilesystem();

  // Marks the blob as deletable, and attempt to purge it.
  zx_status_t QueueUnlink() __TA_EXCLUDES(mutex_);

  // PrepareWrite should be called after allocating a vnode and before writing any data to the blob.
  // The function sets blob size, allocates vmo needed for data and merkle tree, initiates
  // structures needed for compression and reserves an inode for the blob.  It is not meant to be
  // called multiple times on a given vnode.  This is public only for testing.
  zx_status_t PrepareWrite(uint64_t size_data, bool compress) __TA_EXCLUDES(mutex_);

  // If this is part of a migration and involves writing a new blob to replace an old blob, this can
  // be called so that the blob is deleted in the transaction that writes the new blob.  The blob
  // *must* not be currently in use.  It is designed to be used for mount time migrations.
  void SetOldBlob(Blob& blob) __TA_EXCLUDES(mutex_);

  // Sets the target_compression_size in write_info to |size|. Setter made public for testing.
  void SetTargetCompressionSize(uint64_t size) __TA_EXCLUDES(mutex_);

  // Reads in and verifies the contents of this Blob.
  zx_status_t Verify() __TA_EXCLUDES(mutex_);

 private:
  friend class BlobLoaderTest;
  friend class BlobTest;
  DISALLOW_COPY_ASSIGN_AND_MOVE(Blob);

  // Returns whether there are any external references to the blob.
  bool HasReferences() const __TA_REQUIRES_SHARED(mutex_);

  // Identifies if we can safely remove all on-disk and in-memory storage used by this blob.
  // Note that this *must* be called on the main dispatch thread; otherwise the underlying state of
  // the blob could change after (or during) the call, and the blob might not really be purgeable.
  bool Purgeable() const __TA_REQUIRES_SHARED(mutex_) {
    return !HasReferences() && (deletable_ || state_ != BlobState::kReadable);
  }

  // Vnode protected overrides:
  zx_status_t OpenNode(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect) override
      __TA_EXCLUDES(mutex_);
  zx_status_t CloseNode() override __TA_EXCLUDES(mutex_);

  // PagedVnode protected overrides:
  void OnNoPagedVmoClones() override __TA_REQUIRES(mutex_);

  // blobfs::CacheNode implementation:
  BlobCache& GetCache() final;
  bool ShouldCache() const final __TA_EXCLUDES(mutex_);
  void ActivateLowMemory() final __TA_EXCLUDES(mutex_);

  void set_state(BlobState new_state) __TA_REQUIRES(mutex_) { state_ = new_state; }
  BlobState state() const __TA_REQUIRES_SHARED(mutex_) { return state_; }

  // After writing the blob, marks the blob as readable.
  [[nodiscard]] zx_status_t MarkReadable() __TA_REQUIRES(mutex_);

  // Puts a blob into an error state and removes it from the cache so that a client may immediately
  // have another write attempt if required.
  void LatchWriteError(zx_status_t write_error) __TA_REQUIRES(mutex_);

  // Returns a handle to an event which will be signalled when the blob is readable.
  //
  // Returns "ZX_OK" if successful, otherwise the error code will indicate the failure status.
  zx_status_t GetReadableEvent(zx::event* out) __TA_REQUIRES(mutex_);

  // Returns a clone of the blobfs VMO.
  //
  // Monitors the current VMO, keeping a reference to the Vnode alive while the |out| VMO (and any
  // clones it may have) are open.
  zx_status_t CloneDataVmo(zx_rights_t rights, zx::vmo* out_vmo) __TA_REQUIRES(mutex_);

  // Invokes |Purge()| if the vnode is purgeable.
  zx_status_t TryPurge() __TA_REQUIRES(mutex_);

  // Removes all traces of the vnode from blobfs. The blob is not expected to be accessed again
  // after this is called.
  zx_status_t Purge() __TA_REQUIRES(mutex_);

  // Schedules journal transaction prepared by PrepareWrite for the null blob. Null blob doesn't
  // have any data to write. They don't go through regular Write()/WriteInternal path so we
  // explicitly issue journaled write that commits inode allocation and creation.
  zx_status_t WriteNullBlob() __TA_REQUIRES(mutex_);

  // If successful, allocates Blob Node and Blocks (in-memory)
  // kBlobStateEmpty --> kBlobStateDataWrite
  zx_status_t SpaceAllocate() __TA_REQUIRES(mutex_);

  // Write blob data into memory (or stream to disk) and update Merkle tree.
  zx_status_t WriteInternal(const void* data, size_t len, size_t* actual) __TA_REQUIRES(mutex_);

  // Reads from a blob. Requires: kBlobStateReadable
  zx_status_t ReadInternal(void* data, size_t len, size_t off, size_t* actual)
      __TA_EXCLUDES(mutex_);

  // Loads the blob's data and merkle from disk, and initializes the data/merkle VMOs. If paging is
  // enabled, the data VMO will be pager-backed and lazily loaded and verified as the client
  // accesses the pages.
  //
  // If paging is disabled, the entire data VMO is loaded in and verified. Idempotent.
  zx_status_t LoadPagedVmosFromDisk() __TA_REQUIRES(mutex_);
  zx_status_t LoadVmosFromDisk() __TA_REQUIRES(mutex_);

  // Initialize the blob layout for writing.
  zx_status_t InitializeBlobLayout() __TA_REQUIRES(mutex_);

  // Initialize Merkle tree buffers for writing.
  zx_status_t InitializeMerkleBuffer() __TA_REQUIRES(mutex_);

  // Initialize seek table and decompressor when streaming precompressed blobs. Returns true if
  // initialization was successful, false if more data is required to decode the seek table.
  zx::result<bool> InitializeDecompressor(size_t buff_pos) __TA_REQUIRES(mutex_);

  // Verifies the integrity of the null blob (i.e. that its name is correct). Can only be called on
  // the null blob and will assert otherwise.
  zx_status_t VerifyNullBlob() const __TA_REQUIRES_SHARED(mutex_);

  // Called by the Vnode once the last write has completed, updating the on-disk metadata.
  zx_status_t WriteMetadata(BlobTransaction& transaction) __TA_REQUIRES(mutex_);

  // Returns whether the data or merkle tree bytes are mapped and resident in memory.
  bool IsDataLoaded() const __TA_REQUIRES_SHARED(mutex_);

  // Commits the blob to persistent storage.
  zx_status_t Commit() __TA_REQUIRES(mutex_);

  // Returns the block size used by blobfs.
  uint64_t GetBlockSize() const;

  // Flush blob data to persistent storage, and issue commit of metadata. Only blob data is
  // guaranteed to persist on return. Metadata may still remain in cache until next journal flush.
  zx_status_t FlushData() __TA_REQUIRES(mutex_);

  // Write |block_count| blocks at |block_offset| using the data from |producer| into |streamer|.
  zx_status_t WriteData(uint64_t block_count, uint64_t block_offset, BlobDataProducer& producer,
                        fs::DataStreamer& streamer) __TA_REQUIRES(mutex_);

  // Sets the name on the paged_vmo() to indicate where this blob came from. The name will vary
  // according to whether the vmo is currently active to help developers find bugs.
  void SetPagedVmoName(bool active) __TA_REQUIRES(mutex_);

  // Stream as much outstanding data as possible when performing streaming writes. |buff_pos| refers
  // to the offset of the last byte which was buffered in the write buffer VMO. Some data may be
  // flushed to disk to save memory, but this is not guaranteed.
  zx_status_t StreamBufferedData(size_t buff_pos) __TA_REQUIRES(mutex_);

  // When using deprecated blob format with Merkle tree at beginning, ensure outstanding data writes
  // have been issued, and reset block iterator. This ensures only the Merkle tree is outstanding.
  zx_status_t WriteRemainingDataForDeprecatedFormat() __TA_REQUIRES(mutex_);

  Blobfs* const blobfs_;  // Doesn't need locking because this is never changed.
  BlobState state_ __TA_GUARDED(mutex_) = BlobState::kEmpty;
  // True if this node should be unlinked when closed.
  bool deletable_ __TA_GUARDED(mutex_) = false;

  // When paging we can dynamically notice that a blob is corrupt. The read operation will set this
  // flag if a corruption is encoutered at runtime and future operations will fail.
  //
  // This value is specifically atomic and not guarded by the mutex. First, it's not critical that
  // this value be synchronized in any particular way if there are multiple simultaneous readers
  // and one notices the blob is corrupt.
  //
  // Second, this is the only variable written on the read path and avoiding the lock here prevents
  // needing to hold an exclusive lock on the read path. While noticing a corrupt blob is not
  // performance-sensitive, the fidl read path calls recursively into the paging read path and an
  // exclusive lock would deadlock.
  std::atomic<bool> is_corrupt_ = false;  // Not __TA_GUARDED, see above.

  LoaderInfo loader_info_ __TA_GUARDED(mutex_);

  bool tearing_down_ __TA_GUARDED(mutex_) = false;

  enum class SyncingState : char {
    // The Blob is being streamed and it is not possible to generate the merkle root and metadata at
    // this point.
    kDataIncomplete,

    // The blob merkle root is complete but the metadate write has not yet submitted to the
    // underlying media.
    kSyncing,

    // The blob exists on the underlying media.
    kDone,
  };
  // This value is marked kDone on the journal's background thread but read on the main thread so
  // is protected by the mutex.
  SyncingState syncing_state_ __TA_GUARDED(mutex_) = SyncingState::kDataIncomplete;

  uint32_t map_index_ __TA_GUARDED(mutex_) = 0;

  zx::event readable_event_ __TA_GUARDED(mutex_);

  uint64_t blob_size_ __TA_GUARDED(mutex_) = 0;
  uint64_t block_count_ __TA_GUARDED(mutex_) = 0;

  // Data used exclusively during writeback.
  struct WriteInfo;
  std::unique_ptr<WriteInfo> write_info_ __TA_GUARDED(mutex_);
};

// Returns true if the given inode supports paging.
bool SupportsPaging(const Inode& inode);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOB_H_
