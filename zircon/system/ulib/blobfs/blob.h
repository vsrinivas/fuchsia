// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains Vnodes which back a Blobfs filesystem.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_BLOB_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_BLOB_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fuchsia/io/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/promise.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/event.h>
#include <string.h>

#include <atomic>
#include <memory>

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

#include "allocator/allocator.h"
#include "allocator/extent-reserver.h"
#include "allocator/node-reserver.h"
#include "blob-cache.h"
#include "compression/blob-compressor.h"
#include "compression/compressor.h"
#include "format-assertions.h"
#include "metrics.h"
#include "pager/page-watcher.h"

namespace blobfs {

class Blobfs;

using digest::Digest;

typedef uint32_t BlobFlags;

// clang-format off

// After Open:
constexpr BlobFlags kBlobStateEmpty       = 0x00000001; // Not yet allocated
// After Space Reserved (but allocation not yet persisted).
constexpr BlobFlags kBlobStateDataWrite   = 0x00000002; // Data is being written
// After Writing:
constexpr BlobFlags kBlobStateReadable    = 0x00000004; // Readable
// After Unlink:
constexpr BlobFlags kBlobStatePurged      = 0x00000008; // Blob should be released during recycle
// Unrecoverable error state:
constexpr BlobFlags kBlobStateError       = 0x00000010; // Unrecoverable error state
constexpr BlobFlags kBlobStateMask        = 0x000000FF;

// Informational non-state flags:
constexpr BlobFlags kBlobFlagDeletable    = 0x00000100; // This node should be unlinked when closed
constexpr BlobFlags kBlobOtherMask        = 0x0000FF00;

// clang-format on

class Blob final : public CacheNode, fbl::Recyclable<Blob> {
 public:
  // Constructs a blob, reads in data, verifies the contents, then destroys the in-memory copy.
  static zx_status_t VerifyBlob(Blobfs* bs, uint32_t node_index);

  // Constructs actual blobs
  Blob(Blobfs* bs, const Digest& digest);
  virtual ~Blob();

  ////////////////
  // fs::Vnode interface.

  using fs::Vnode::Open;
  zx_status_t Open(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect) final;
  zx_status_t Close() final;

  ////////////////
  // fbl::Recyclable interface.

  void fbl_recycle() final { CacheNode::fbl_recycle(); }

  ////////////////
  // Other methods.

  BlobFlags GetState() const { return flags_ & kBlobStateMask; }

  // Identifies if we can safely remove all on-disk and in-memory storage used
  // by this blob.
  bool Purgeable() const {
    return fd_count_ == 0 && (DeletionQueued() || !(GetState() & kBlobStateReadable));
  }

  bool DeletionQueued() const { return flags_ & kBlobFlagDeletable; }

  void SetState(BlobFlags new_state) { flags_ = (flags_ & ~kBlobStateMask) | new_state; }

  uint32_t GetMapIndex() const { return map_index_; }

  // Returns a unique identifier for this blob
  size_t Ino() const { return map_index_; }

  void PopulateInode(uint32_t node_index);

  uint64_t SizeData() const;

  const Inode& GetNode() const { return inode_; }

  void CompleteSync();

  // When blob VMOs are cloned and returned to clients, blobfs watches
  // the original VMO handle for the signal |ZX_VMO_ZERO_CHILDREN|.
  // While this signal is not set, the blob's Vnode keeps an extra
  // reference to itself to prevent teardown while clients are using
  // this Vmo. This reference is internally called the "clone watcher".
  //
  // This function may be called on a blob to tell it to forcefully release
  // the "reference to itself" that is kept when the blob is mapped.
  //
  // Returns this reference, if it exists, to provide control over
  // when the Vnode destructor is executed.
  fbl::RefPtr<Blob> CloneWatcherTeardown();

  // Marks the blob as deletable, and attempt to purge it.
  zx_status_t QueueUnlink();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Blob);

  ////////////////
  // fs::Vnode interface.

  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final;
  fs::VnodeProtocolSet GetProtocols() const final;
  bool ValidateRights(fs::Rights rights) final;
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) final;
  zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t Truncate(size_t len) final;
  zx_status_t QueryFilesystem(llcpp::fuchsia::io::FilesystemInfo* out) final;
  zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) final;
  zx_status_t GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) final;
  void Sync(SyncCallback closure) final;

  ////////////////
  // blobfs::CacheNode interface.

  BlobCache& Cache() final;
  bool ShouldCache() const final;
  void ActivateLowMemory() final;

  ////////////////
  // Other methods.

  void BlobCloseHandles();

  // Returns a handle to an event which will be signalled when
  // the blob is readable.
  //
  // Returns "ZX_OK" if successful, otherwise the error code
  // will indicate the failure status.
  zx_status_t GetReadableEvent(zx::event* out);

  // Returns a clone of the blobfs VMO.
  //
  // Monitors the current VMO, keeping a reference to the Vnode
  // alive while the |out| VMO (and any clones it may have) are open.
  zx_status_t CloneVmo(zx_rights_t rights, zx::vmo* out_vmo, size_t* out_size);
  void HandleNoClones(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                      const zx_packet_signal_t* signal);

  // Invokes |Purge()| if the vnode is purgeable.
  zx_status_t TryPurge();

  // Removes all traces of the vnode from blobfs.
  // The blob is not expected to be accessed again after this is called.
  zx_status_t Purge();

  // If successful, allocates Blob Node and Blocks (in-memory)
  // kBlobStateEmpty --> kBlobStateDataWrite
  zx_status_t SpaceAllocate(uint64_t size_data);

  // Writes to either the Merkle Tree or the Data section,
  // depending on the state.
  zx_status_t WriteInternal(const void* data, size_t len, size_t* actual);

  // For a blob being written, consider stopping the compressor,
  // the blob to eventually be written uncompressed to disk.
  //
  // For blobs which don't compress very well, this provides an escape
  // hatch to avoid wasting work.
  void ConsiderCompressionAbort();

  // Reads from a blob.
  // Requires: kBlobStateReadable
  zx_status_t ReadInternal(void* data, size_t len, size_t off, size_t* actual);

  // Reads both VMOs into memory, if we haven't already.
  //
  // TODO(ZX-1481): When we have can register the Blob Store as a pager
  // service, and it can properly handle pages faults on a vnode's contents,
  // then we can avoid reading the entire blob up-front. Until then, read
  // the contents of a VMO into memory when it is opened.
  zx_status_t InitVmos();

  // Initializes a compressed blob by reading it from disk and decompressing it.
  // Does not verify the blob.
  zx_status_t InitCompressed(CompressionAlgorithm algorithm);

  // Initializes a decompressed blob by reading it from disk.
  // Does not verify the blob.
  zx_status_t InitUncompressed();

  // Verifies the integrity of the in-memory Blob - operates on the entire blob at once.
  // InitVmos() must have already been called for this blob.
  zx_status_t Verify() const;

  // Called by the Vnode once the last write has completed, updating the
  // on-disk metadata.
  fit::promise<void, zx_status_t> WriteMetadata();

  // Acquires a pointer to the mapped data or merkle tree
  void* GetData() const;
  void* GetMerkle() const;

  Blobfs* const blobfs_;
  BlobFlags flags_ = {};
  std::atomic_bool syncing_;

  // The mapping here consists of:
  // 1) The Merkle Tree
  // 2) The Blob itself, aligned to the nearest kBlobfsBlockSize
  fzl::OwnedVmoMapper mapping_;
  vmoid_t vmoid_ = {};

  // Watches any clones of "vmo_" provided to clients.
  // Observes the ZX_VMO_ZERO_CHILDREN signal.
  async::WaitMethod<Blob, &Blob::HandleNoClones> clone_watcher_;
  // Keeps a reference to the blob alive (from within itself)
  // until there are no cloned VMOs in used.
  //
  // This RefPtr is only non-null when a client is using a cloned VMO,
  // or there would be a clear leak of Blob.
  fbl::RefPtr<Blob> clone_ref_ = {};

  zx::event readable_event_ = {};

  uint32_t fd_count_ = {};
  uint32_t map_index_ = {};

  // TODO(smklein): We are only using a few of these fields, such as:
  // - blob_size
  // - block_count
  // To save space, we could avoid holding onto the entire inode.
  Inode inode_ = {};

  // Data used exclusively during writeback.
  struct WritebackInfo {
    uint64_t bytes_written = {};

    fbl::Vector<ReservedExtent> extents;
    fbl::Vector<ReservedNode> node_indices;

    std::optional<BlobCompressor> compressor;
  };

  std::unique_ptr<WritebackInfo> write_info_ = {};

  // Reads in the blob's pages on demand.
  std::unique_ptr<PageWatcher> page_watcher_ = nullptr;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_BLOB_H_
