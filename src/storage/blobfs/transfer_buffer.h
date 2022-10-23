// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TRANSFER_BUFFER_H_
#define SRC_STORAGE_BLOBFS_TRANSFER_BUFFER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/zx/result.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <fbl/macros.h>
#include <storage/buffer/owned_vmoid.h>

#include "src/storage/blobfs/blobfs_metrics.h"
#include "src/storage/blobfs/iterator/block_iterator_provider.h"
#include "src/storage/blobfs/loader_info.h"
#include "src/storage/blobfs/node_finder.h"
#include "src/storage/blobfs/transaction_manager.h"

namespace blobfs {

// The size of the transfer buffer for reading from storage.
//
// The decision to use a single global transfer buffer is arbitrary; a pool of them could also be
// available in the future for more fine-grained access. Moreover, the blobfs pager uses a single
// thread at the moment, so a global buffer should be sufficient.
//
// 256 MB; but the size is arbitrary, since pages will become decommitted as they are moved to
// destination VMOS.
constexpr uint64_t kTransferBufferSize = 256 * (1ull << 20);

// The size of the scratch buffer used for decompression. Must be big enough to hold the largest
// decompressed chunk of a blob.
//
// The decision to use a single global transfer buffer is arbitrary; a pool of them could also be
// available in the future for more fine-grained access. Moreover, the blobfs pager uses a single
// thread at the moment, so a global buffer should be sufficient.
//
// 256 MB; but the size is arbitrary, since pages will become decommitted as they are moved to
// destination VMOS.
constexpr uint64_t kDecompressionBufferSize = 256 * (1ull << 20);

// Make sure blocks are page-aligned.
static_assert(kBlobfsBlockSize % PAGE_SIZE == 0);

// Make sure the pager transfer buffer is block-aligned.
static_assert(kTransferBufferSize % kBlobfsBlockSize == 0);

// Make sure the decompression scratch buffer is block-aligned.
static_assert(kDecompressionBufferSize % kBlobfsBlockSize == 0);

// Make sure the pager transfer buffer and decompression buffer are sized per the worst case
// compression ratio of 1.
static_assert(kTransferBufferSize >= kDecompressionBufferSize);

// TransferBuffer is an interface representing a transfer buffer which can be loaded with data from
// the underlying storage device.
//
// The VMO returned by |TransferBuffer::vmo()| is guaranteed to never be mapped by the instance,
// which makes the VMO suitable for use with |zx_pager_supply_pages|.
class TransferBuffer {
 public:
  virtual ~TransferBuffer() = default;

  // Loads the buffer with data from the inode corresponding to |info.identifier|, at the byte range
  // specified by [|offset|, |offset| + |length|).
  // |offset| must be block aligned. |length| may be rounded up to a block-aligned offset.
  [[nodiscard]] virtual zx::result<> Populate(uint64_t offset, uint64_t length,
                                              const LoaderInfo& info) = 0;

  // Accesses the underlying VMO.
  // Must be preceded with a call to |TransferBuffer::Populate()|. The contents of the returned
  // VMO are only defined up to |length| bytes (the value passed to the last call to
  // |TransferBuffer::Populate()|).
  virtual const zx::vmo& GetVmo() const = 0;

  // Returns the size of the underlying VMO.
  virtual size_t GetSize() const = 0;
};

// StorageBackedTransferBuffer is an instance of |TransferBuffer| which can be loaded with data from
// the underlying storage device.
class StorageBackedTransferBuffer : public TransferBuffer {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(StorageBackedTransferBuffer);

  // Creates an instance of |StorageBackedTransferBuffer| with a VMO of size |size| bytes.
  // |size| must be a multiple of the block size of the underlying storage device.
  [[nodiscard]] static zx::result<std::unique_ptr<StorageBackedTransferBuffer>> Create(
      size_t size, TransactionManager* txn_manager, BlockIteratorProvider* block_iter_provider,
      BlobfsMetrics* metrics);

  [[nodiscard]] zx::result<> Populate(uint64_t offset, uint64_t length,
                                      const LoaderInfo& info) final;

  const zx::vmo& GetVmo() const final { return vmo_; }
  size_t GetSize() const final { return size_; }

 private:
  StorageBackedTransferBuffer(zx::vmo vmo, size_t size, storage::OwnedVmoid vmoid,
                              TransactionManager* txn_manager,
                              BlockIteratorProvider* block_iter_provider, BlobfsMetrics* metrics);

  TransactionManager* txn_manager_ = nullptr;
  BlockIteratorProvider* block_iter_provider_ = nullptr;

  zx::vmo vmo_;
  const size_t size_;
  storage::OwnedVmoid vmoid_;
  BlobfsMetrics* metrics_ = nullptr;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TRANSFER_BUFFER_H_
