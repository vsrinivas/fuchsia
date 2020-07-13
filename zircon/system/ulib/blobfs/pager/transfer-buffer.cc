// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transfer-buffer.h"

namespace blobfs {
namespace pager {

StorageBackedTransferBuffer::StorageBackedTransferBuffer(zx::vmo vmo, storage::OwnedVmoid vmoid,
                                                         TransactionManager* txn_manager,
                                                         BlockIteratorProvider* block_iter_provider,
                                                         BlobfsMetrics* metrics)
    : txn_manager_(txn_manager),
      block_iter_provider_(block_iter_provider),
      vmo_(std::move(vmo)),
      vmoid_(std::move(vmoid)),
      metrics_(metrics) {}

zx::status<std::unique_ptr<StorageBackedTransferBuffer>> StorageBackedTransferBuffer::Create(
    size_t size, TransactionManager* txn_manager, BlockIteratorProvider* block_iter_provider,
    BlobfsMetrics* metrics) {
  ZX_DEBUG_ASSERT(metrics != nullptr && txn_manager != nullptr && block_iter_provider != nullptr);
  if (size % kBlobfsBlockSize != 0 || size % PAGE_SIZE != 0) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot create pager transfer buffer: %s\n",
                   zx_status_get_string(status));
    return zx::error(status);
  }
  storage::OwnedVmoid vmoid(txn_manager);
  status = vmoid.AttachVmo(vmo);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to attach pager transfer vmo: %s\n",
                   zx_status_get_string(status));
    return zx::error(status);
  }

  return zx::ok(std::unique_ptr<StorageBackedTransferBuffer>(new StorageBackedTransferBuffer(
      std::move(vmo), std::move(vmoid), txn_manager, block_iter_provider, metrics)));
}

zx::status<> StorageBackedTransferBuffer::Populate(uint64_t offset, uint64_t length,
                                                   const UserPagerInfo& info) {
  fs::Ticker ticker(metrics_->Collecting());
  if (offset % kBlobfsBlockSize != 0) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fs::ReadTxn txn(txn_manager_);
  BlockIterator block_iter = block_iter_provider_->BlockIteratorByNodeIndex(info.identifier);

  auto start_block = static_cast<uint32_t>((offset + info.data_start_bytes) / kBlobfsBlockSize);
  auto block_count =
      static_cast<uint32_t>(fbl::round_up(length, kBlobfsBlockSize) / kBlobfsBlockSize);

  // Navigate to the start block.
  zx_status_t status = IterateToBlock(&block_iter, start_block);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to navigate to start block %u: %s\n", start_block,
                   zx_status_get_string(status));
    return zx::error(status);
  }

  // Enqueue operations to read in the required blocks to the transfer buffer.
  const uint64_t data_start = DataStartBlock(txn_manager_->Info());
  status = StreamBlocks(
      &block_iter, block_count, [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
        txn.Enqueue(vmoid_.get(), vmo_offset - start_block, dev_offset + data_start, length);
        return ZX_OK;
      });
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to enqueue read operations: %s\n", zx_status_get_string(status));
    return zx::error(status);
  }

  // Issue the read.
  status = txn.Transact();
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to transact read operations: %s\n",
                   zx_status_get_string(status));
    return zx::error(status);
  }

  // Update read metrics
  if (info.decompressor == nullptr) {
    metrics_->paged_read_metrics().IncrementDiskRead(CompressionAlgorithm::UNCOMPRESSED,
                                                     block_count * kBlobfsBlockSize, ticker.End());
  } else {
    // TODO(xbhatnag): Get the correct compression algorithm. We're making an assumption here.
    metrics_->paged_read_metrics().IncrementDiskRead(CompressionAlgorithm::CHUNKED,
                                                     block_count * kBlobfsBlockSize, ticker.End());
  }
  return zx::ok();
}

}  // namespace pager
}  // namespace blobfs
