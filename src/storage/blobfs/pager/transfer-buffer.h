// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_PAGER_TRANSFER_BUFFER_H_
#define SRC_STORAGE_BLOBFS_PAGER_TRANSFER_BUFFER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <blobfs/node-finder.h>
#include <fbl/macros.h>
#include <storage/buffer/owned_vmoid.h>

#include "../iterator/block-iterator-provider.h"
#include "../metrics.h"
#include "../transaction-manager.h"
#include "user-pager-info.h"

namespace blobfs {
namespace pager {

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
  [[nodiscard]] virtual zx::status<> Populate(uint64_t offset, uint64_t length,
                                              const UserPagerInfo& info) = 0;

  // Accesses the underlying VMO.
  // Must be preceded with a call to |TransferBuffer::Populate()|. The contents of the returned
  // VMO are only defined up to |length| bytes (the value passed to the last call to
  // |TransferBuffer::Populate()|).
  virtual const zx::vmo& vmo() const = 0;
};

// StorageBackedTransferBuffer is an instance of |TransferBuffer| which can be loaded with data from
// the underlying storage device.
class StorageBackedTransferBuffer : public TransferBuffer {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(StorageBackedTransferBuffer);

  // Creates an instance of |StorageBackedTransferBuffer| with a VMO of size |size| bytes.
  // |size| must be a multiple of the block size of the underlying storage device.
  [[nodiscard]] static zx::status<std::unique_ptr<StorageBackedTransferBuffer>> Create(
      size_t size, TransactionManager* txn_manager, BlockIteratorProvider* block_iter_provider,
      BlobfsMetrics* metrics);

  [[nodiscard]] zx::status<> Populate(uint64_t offset, uint64_t length,
                                      const UserPagerInfo& info) final;

  const zx::vmo& vmo() const final { return vmo_; }

 private:
  StorageBackedTransferBuffer(zx::vmo vmo, storage::OwnedVmoid vmoid,
                              TransactionManager* txn_manager,
                              BlockIteratorProvider* block_iter_provider, BlobfsMetrics* metrics);

  TransactionManager* txn_manager_ = nullptr;
  BlockIteratorProvider* block_iter_provider_ = nullptr;

  zx::vmo vmo_;
  storage::OwnedVmoid vmoid_;
  BlobfsMetrics* metrics_ = nullptr;
};

}  // namespace pager
}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_PAGER_TRANSFER_BUFFER_H_
