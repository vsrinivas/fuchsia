// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob-loader.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <memory>

#include <blobfs/blob-layout.h>
#include <blobfs/common.h>
#include <blobfs/compression-settings.h>
#include <blobfs/format.h>
#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <storage/buffer/owned_vmoid.h>

#include "blob-verifier.h"
#include "compression/chunked.h"
#include "compression/decompressor.h"
#include "compression/seekable-decompressor.h"
#include "iterator/block-iterator.h"

namespace blobfs {

namespace {

// TODO(jfsulliv): Rationalize this with the size limits for chunk-compression headers.
constexpr size_t kScratchBufferSize = 4 * kBlobfsBlockSize;

}  // namespace

BlobLoader::BlobLoader(TransactionManager* txn_manager, BlockIteratorProvider* block_iter_provider,
                       NodeFinder* node_finder, pager::UserPager* pager, BlobfsMetrics* metrics,
                       fzl::OwnedVmoMapper scratch_vmo)
    : txn_manager_(txn_manager),
      block_iter_provider_(block_iter_provider),
      node_finder_(node_finder),
      pager_(pager),
      metrics_(metrics),
      scratch_vmo_(std::move(scratch_vmo)) {}

zx::status<BlobLoader> BlobLoader::Create(TransactionManager* txn_manager,
                                          BlockIteratorProvider* block_iter_provider,
                                          NodeFinder* node_finder, pager::UserPager* pager,
                                          BlobfsMetrics* metrics) {
  fzl::OwnedVmoMapper scratch_vmo;
  zx_status_t status = scratch_vmo.CreateAndMap(kScratchBufferSize, "blobfs-loader");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to map scratch vmo: %s\n", zx_status_get_string(status));
    return zx::error(status);
  }
  return zx::ok(BlobLoader(txn_manager, block_iter_provider, node_finder, pager, metrics,
                           std::move(scratch_vmo)));
}

void BlobLoader::Reset() { scratch_vmo_.Reset(); }

zx_status_t BlobLoader::LoadBlob(uint32_t node_index,
                                 const BlobCorruptionNotifier* corruption_notifier,
                                 fzl::OwnedVmoMapper* data_out, fzl::OwnedVmoMapper* merkle_out) {
  ZX_DEBUG_ASSERT(scratch_vmo_.vmo().is_valid());
  const InodePtr inode = node_finder_->GetNode(node_index);
  // LoadBlob should only be called for Inodes. If this doesn't hold, one of two things happened:
  //   - Programmer error
  //   - Corruption of a blob's Inode
  // In either case it is preferable to ASSERT than to return an error here, since the first case
  // should happen only during development and in the second case there may be more corruption and
  // we want to unmount the filesystem before any more damage is done.
  ZX_ASSERT(inode->header.IsInode() && inode->header.IsAllocated());

  TRACE_DURATION("blobfs", "BlobLoader::LoadBlob", "blob_size", inode->blob_size);

  auto blob_layout = BlobLayout::CreateFromInode(GetBlobLayoutFormat(txn_manager_->Info()), *inode,
                                                 GetBlockSize());
  if (blob_layout.is_error()) {
    FS_TRACE_ERROR("blobfs: Failed to create blob layout: %s\n", blob_layout.status_string());
    return blob_layout.status_value();
  }
  if (inode->blob_size == 0) {
    // No data to load for the null blob.
    return ZX_OK;
  }

  fzl::OwnedVmoMapper merkle_mapper;
  std::unique_ptr<BlobVerifier> verifier;
  zx_status_t status;
  if ((status = InitMerkleVerifier(node_index, *inode, *blob_layout.value(), corruption_notifier,
                                   &merkle_mapper, &verifier)) != ZX_OK) {
    return status;
  }

  uint64_t file_block_aligned_size = blob_layout->FileBlockAlignedSize();
  fbl::StringBuffer<ZX_MAX_NAME_LEN> data_vmo_name;
  FormatBlobDataVmoName(node_index, &data_vmo_name);

  fzl::OwnedVmoMapper data_mapper;
  status = data_mapper.CreateAndMap(file_block_aligned_size, data_vmo_name.c_str());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to initialize data vmo; error: %s\n",
                   zx_status_get_string(status));
    return status;
  }
  status = inode->IsCompressed()
               ? LoadAndDecompressData(node_index, *inode, *blob_layout.value(), data_mapper)
               : LoadData(node_index, *blob_layout.value(), data_mapper);
  if (status != ZX_OK) {
    return status;
  }

  if ((status = verifier->Verify(data_mapper.start(), inode->blob_size, file_block_aligned_size)) !=
      ZX_OK) {
    return status;
  }

  *data_out = std::move(data_mapper);
  if (merkle_mapper.vmo().is_valid()) {
    *merkle_out = std::move(merkle_mapper);
  }
  return ZX_OK;
}

zx_status_t BlobLoader::LoadBlobPaged(uint32_t node_index,
                                      const BlobCorruptionNotifier* corruption_notifier,
                                      std::unique_ptr<pager::PageWatcher>* page_watcher_out,
                                      fzl::OwnedVmoMapper* data_out,
                                      fzl::OwnedVmoMapper* merkle_out) {
  ZX_DEBUG_ASSERT(scratch_vmo_.vmo().is_valid());
  const InodePtr inode = node_finder_->GetNode(node_index);
  // LoadBlobPaged should only be called for Inodes. If this doesn't hold, one of two things
  // happened:
  //   - Programmer error
  //   - Corruption of a blob's Inode
  // In either case it is preferable to ASSERT than to return an error here, since the first case
  // should happen only during development and in the second case there may be more corruption and
  // we want to unmount the filesystem before any more damage is done.
  ZX_ASSERT(inode->header.IsInode() && inode->header.IsAllocated());

  TRACE_DURATION("blobfs", "BlobLoader::LoadBlobPaged", "blob_size", inode->blob_size);

  auto blob_layout = BlobLayout::CreateFromInode(GetBlobLayoutFormat(txn_manager_->Info()), *inode,
                                                 GetBlockSize());
  if (blob_layout.is_error()) {
    FS_TRACE_ERROR("blobfs: Failed to create blob layout: %s\n",
                   zx_status_get_string(blob_layout.error_value()));
    return blob_layout.error_value();
  }
  if (inode->blob_size == 0) {
    // No data to load for the null blob.
    return ZX_OK;
  }

  fzl::OwnedVmoMapper merkle_mapper;
  std::unique_ptr<BlobVerifier> verifier;
  zx_status_t status;
  if ((status = InitMerkleVerifier(node_index, *inode, *blob_layout.value(), corruption_notifier,
                                   &merkle_mapper, &verifier)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<SeekableDecompressor> decompressor;
  if ((status = InitForDecompression(node_index, *inode, *blob_layout.value(), *verifier,
                                     &decompressor)) != ZX_OK) {
    return status;
  }

  pager::UserPagerInfo userpager_info;
  userpager_info.identifier = node_index;
  userpager_info.data_start_bytes = uint64_t{blob_layout->DataBlockOffset()} * GetBlockSize();
  userpager_info.data_length_bytes = inode->blob_size;
  userpager_info.verifier = std::move(verifier);
  userpager_info.decompressor = std::move(decompressor);
  auto page_watcher = std::make_unique<pager::PageWatcher>(pager_, std::move(userpager_info));

  fbl::StringBuffer<ZX_MAX_NAME_LEN> data_vmo_name;
  FormatBlobDataVmoName(node_index, &data_vmo_name);

  zx::vmo data_vmo;
  if ((status = page_watcher->CreatePagedVmo(blob_layout->FileBlockAlignedSize(), &data_vmo)) !=
      ZX_OK) {
    return status;
  }
  data_vmo.set_property(ZX_PROP_NAME, data_vmo_name.c_str(), data_vmo_name.length());

  fzl::OwnedVmoMapper data_mapper;
  if ((status = data_mapper.Map(std::move(data_vmo))) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to create mapping for data vmo: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  *page_watcher_out = std::move(page_watcher);
  *data_out = std::move(data_mapper);
  if (merkle_mapper.vmo().is_valid()) {
    *merkle_out = std::move(merkle_mapper);
  }
  return ZX_OK;
}

zx_status_t BlobLoader::InitMerkleVerifier(uint32_t node_index, const Inode& inode,
                                           const BlobLayout& blob_layout,
                                           const BlobCorruptionNotifier* notifier,
                                           fzl::OwnedVmoMapper* vmo_out,
                                           std::unique_ptr<BlobVerifier>* verifier_out) {
  if (blob_layout.MerkleTreeSize() == 0) {
    return BlobVerifier::CreateWithoutTree(digest::Digest(inode.merkle_root_hash), metrics_,
                                           inode.blob_size, notifier, verifier_out);
  }

  fzl::OwnedVmoMapper merkle_mapper;
  std::unique_ptr<BlobVerifier> verifier;

  fbl::StringBuffer<ZX_MAX_NAME_LEN> merkle_vmo_name;
  FormatBlobMerkleVmoName(node_index, &merkle_vmo_name);

  zx_status_t status;
  if ((status = merkle_mapper.CreateAndMap(blob_layout.MerkleTreeBlockAlignedSize(),
                                           merkle_vmo_name.c_str())) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to initialize merkle vmo; error: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  if ((status = LoadMerkle(node_index, blob_layout, merkle_mapper)) != ZX_OK) {
    return status;
  }

  // The Merkle tree may not start at the beginning of the vmo in the kCompactMerkleTreeAtEnd
  // format.
  void* merkle_tree_start = static_cast<uint8_t*>(merkle_mapper.start()) +
                            blob_layout.MerkleTreeOffsetWithinBlockOffset();

  if ((status = BlobVerifier::Create(digest::Digest(inode.merkle_root_hash), metrics_,
                                     merkle_tree_start, blob_layout.MerkleTreeSize(),
                                     blob_layout.Format(), inode.blob_size, notifier, &verifier)) !=
      ZX_OK) {
    return status;
  }

  *vmo_out = std::move(merkle_mapper);
  *verifier_out = std::move(verifier);
  return ZX_OK;
}

zx_status_t BlobLoader::InitForDecompression(
    uint32_t node_index, const Inode& inode, const BlobLayout& blob_layout,
    const BlobVerifier& verifier, std::unique_ptr<SeekableDecompressor>* decompressor_out) {
  zx::status<CompressionAlgorithm> algorithm_status = AlgorithmForInode(inode);
  if (algorithm_status.is_error()) {
    FS_TRACE_ERROR("blobfs: Cannot decode blob due to multiple compression flags.\n");
    return algorithm_status.status_value();
  }
  CompressionAlgorithm algorithm = algorithm_status.value();

  switch (algorithm) {
    case CompressionAlgorithm::UNCOMPRESSED:
      return ZX_OK;
    case CompressionAlgorithm::CHUNKED:
      break;
    case CompressionAlgorithm::LZ4:
    case CompressionAlgorithm::ZSTD:
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      // Callers should have guarded against calling this code path with an algorithm that
      // does not support paging.
      FS_TRACE_ERROR(
          "Algorithm %s does not support paging; this path should not be called.\n"
          "This is most likely programmer error.\n",
          CompressionAlgorithmToString(algorithm));
      ZX_DEBUG_ASSERT(false);
      return ZX_ERR_NOT_SUPPORTED;
  }

  TRACE_DURATION("blobfs", "BlobLoader::InitDecompressor");

  // The first few blocks of data contain the seek table, which we need to read to initialize
  // the decompressor. Read these from disk.

  uint32_t data_block_count = blob_layout.DataBlockCount();
  // We don't know exactly how long the header is, so fill up as much of the scratch VMO as we can.
  // (The header should never be bigger than the size of the scratch VMO.)
  ZX_DEBUG_ASSERT(scratch_vmo_.size() % GetBlockSize() == 0);
  uint32_t scratch_block_count = static_cast<uint32_t>(scratch_vmo_.size()) / GetBlockSize();
  uint32_t blocks_to_read = std::min(scratch_block_count, data_block_count);
  if (blocks_to_read == 0) {
    FS_TRACE_ERROR("blobfs: No data blocks; corrupted inode?\n");
    return ZX_ERR_BAD_STATE;
  }

  auto bytes_read =
      LoadBlocks(node_index, blob_layout.DataBlockOffset(), blocks_to_read, scratch_vmo_);
  if (bytes_read.is_error()) {
    FS_TRACE_ERROR("blobfs: Failed to load compression header: %s\n", bytes_read.status_string());
    return bytes_read.error_value();
  }

  zx_status_t status;
  // If we read all of the blob's data into the scratch VMO then the scratch VMO may contain part of
  // the Merkle tree that should be removed.
  if (blocks_to_read == data_block_count) {
    ZeroMerkleTreeWithinDataVmo(scratch_vmo_, blob_layout);
  }

  if ((status = SeekableChunkedDecompressor::CreateDecompressor(
           scratch_vmo_.start(), uint64_t{blocks_to_read} * GetBlockSize(), inode.blob_size,
           decompressor_out)) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to init decompressor: %s\n", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t BlobLoader::LoadMerkle(uint32_t node_index, const BlobLayout& blob_layout,
                                   const fzl::OwnedVmoMapper& vmo) const {
  fs::Ticker ticker(metrics_->Collecting());
  auto bytes_read = LoadBlocks(node_index, blob_layout.MerkleTreeBlockOffset(),
                               blob_layout.MerkleTreeBlockCount(), vmo);
  if (bytes_read.is_error()) {
    FS_TRACE_ERROR("blobfs: Failed to load Merkle tree: %s\n", bytes_read.status_string());
    return bytes_read.error_value();
  }

  metrics_->IncrementMerkleDiskRead(bytes_read.value(), ticker.End());
  return ZX_OK;
}

zx_status_t BlobLoader::LoadData(uint32_t node_index, const BlobLayout& blob_layout,
                                 const fzl::OwnedVmoMapper& vmo) const {
  TRACE_DURATION("blobfs", "BlobLoader::LoadData");

  fs::Ticker ticker(metrics_->Collecting());
  auto bytes_read =
      LoadBlocks(node_index, blob_layout.DataBlockOffset(), blob_layout.DataBlockCount(), vmo);
  if (bytes_read.is_error()) {
    return bytes_read.error_value();
  }
  metrics_->unpaged_read_metrics().IncrementDiskRead(CompressionAlgorithm::UNCOMPRESSED,
                                                     bytes_read.value(), ticker.End());

  ZeroMerkleTreeWithinDataVmo(vmo, blob_layout);
  return ZX_OK;
}

zx_status_t BlobLoader::LoadAndDecompressData(uint32_t node_index, const Inode& inode,
                                              const BlobLayout& blob_layout,
                                              const fzl::OwnedVmoMapper& vmo) const {
  zx::status<CompressionAlgorithm> algorithm_or = AlgorithmForInode(inode);
  if (algorithm_or.is_error()) {
    FS_TRACE_ERROR("Blob has no known compression format\n");
    return algorithm_or.status_value();
  }
  CompressionAlgorithm algorithm = algorithm_or.value();
  ZX_DEBUG_ASSERT(algorithm != CompressionAlgorithm::UNCOMPRESSED);

  TRACE_DURATION("blobfs", "BlobLoader::LoadAndDecompressData", "compressed_size",
                 blob_layout.DataSizeUpperBound(), "blob_size", inode.blob_size);

  // Create and attach a transfer VMO for fetching the compressed contents from the block FIFO.
  fbl::StringBuffer<ZX_MAX_NAME_LEN> vmo_name;
  FormatBlobCompressedVmoName(node_index, &vmo_name);
  fzl::OwnedVmoMapper compressed_mapper;
  zx_status_t status =
      compressed_mapper.CreateAndMap(blob_layout.DataBlockAlignedSize(), vmo_name.c_str());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to initialized compressed vmo; error: %d\n", status);
    return status;
  }

  fs::Ticker read_ticker(metrics_->Collecting());
  auto bytes_read = LoadBlocks(node_index, blob_layout.DataBlockOffset(),
                               blob_layout.DataBlockCount(), compressed_mapper);
  if (bytes_read.is_error()) {
    return bytes_read.error_value();
  }
  metrics_->unpaged_read_metrics().IncrementDiskRead(algorithm, bytes_read.value(),
                                                     read_ticker.End());

  ZeroMerkleTreeWithinDataVmo(compressed_mapper, blob_layout);

  fs::Ticker ticker(metrics_->Collecting());

  // Decompress into the target buffer.
  size_t target_size = inode.blob_size;
  const void* compressed_buffer = compressed_mapper.start();
  std::unique_ptr<Decompressor> decompressor;
  status = Decompressor::Create(algorithm, &decompressor);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to create decompressor, status=%d", status);
    return status;
  }

  status = decompressor->Decompress(vmo.start(), &target_size, compressed_buffer,
                                    blob_layout.DataSizeUpperBound());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to decompress data: %s\n", zx_status_get_string(status));
    return status;
  } else if (target_size != inode.blob_size) {
    FS_TRACE_ERROR("Failed to fully decompress blob (%zu of %zu expected)\n", target_size,
                   inode.blob_size);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  metrics_->unpaged_read_metrics().IncrementDecompression(algorithm, inode.blob_size, ticker.End());

  return ZX_OK;
}

zx::status<uint64_t> BlobLoader::LoadBlocks(uint32_t node_index, uint32_t block_offset,
                                            uint32_t block_count,
                                            const fzl::OwnedVmoMapper& vmo) const {
  TRACE_DURATION("blobfs", "BlobLoader::LoadBlocks", "block_count", block_count);

  zx_status_t status;
  // Attach |vmo| for transfer to the block FIFO.
  storage::OwnedVmoid vmoid(txn_manager_);
  if ((status = vmoid.AttachVmo(vmo.vmo())) != ZX_OK) {
    FS_TRACE_ERROR("Failed to attach VMO to block device; error: %s\n",
                   zx_status_get_string(status));
    return zx::error(status);
  }

  fs::ReadTxn txn(txn_manager_);

  const uint64_t kDataStart = DataStartBlock(txn_manager_->Info());
  BlockIterator block_iter = block_iter_provider_->BlockIteratorByNodeIndex(node_index);
  if ((status = IterateToBlock(&block_iter, block_offset)) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to seek to starting block: %s\n", zx_status_get_string(status));
    return zx::error(status);
  }

  status = StreamBlocks(
      &block_iter, block_count, [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
        txn.Enqueue(vmoid.get(), vmo_offset - block_offset, kDataStart + dev_offset, length);
        return ZX_OK;
      });
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to stream blocks: %s\n", zx_status_get_string(status));
    return zx::error(status);
  }
  if ((status = txn.Transact()) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to flush read transaction: %s\n", zx_status_get_string(status));
    return zx::error(status);
  }

  return zx::ok(uint64_t{block_count} * GetBlockSize());
}

void BlobLoader::ZeroMerkleTreeWithinDataVmo(const fzl::OwnedVmoMapper& vmo,
                                             const BlobLayout& blob_layout) const {
  if (!blob_layout.HasMerkleTreeAndDataSharedBlock()) {
    return;
  }
  uint64_t data_block_aligned_size = blob_layout.DataBlockAlignedSize();
  ZX_DEBUG_ASSERT(vmo.size() >= data_block_aligned_size);
  uint64_t len = uint64_t{GetBlockSize()} - blob_layout.MerkleTreeOffsetWithinBlockOffset();
  // Since the block is shared, data_block_aligned_size is >= 1 block.
  uint64_t offset = data_block_aligned_size - len;
  memset(static_cast<uint8_t*>(vmo.start()) + offset, 0, len);
}

uint32_t BlobLoader::GetBlockSize() const { return txn_manager_->Info().block_size; }

}  // namespace blobfs
