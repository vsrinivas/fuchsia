// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob-loader.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <memory>

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <storage/buffer/owned_vmoid.h>

#include "blob-verifier.h"
#include "compression/algorithm.h"
#include "compression/decompressor.h"
#include "iterator/block-iterator.h"

namespace blobfs {

BlobLoader::BlobLoader(Blobfs* const blobfs, UserPager* const pager)
  : blobfs_(blobfs), pager_(pager) {}

zx_status_t BlobLoader::LoadBlob(uint32_t node_index, fzl::OwnedVmoMapper* data_out,
                                 fzl::OwnedVmoMapper* merkle_out) {
  const Inode* const inode = blobfs_->GetNode(node_index);
  // LoadBlob should only be called for Inodes. If this doesn't hold, one of two things happened:
  //   - Programmer error
  //   - Corruption of a blob's Inode
  // In either case it is preferable to ASSERT than to return an error here, since the first case
  // should happen only during development and in the second case there may be more corruption and
  // we want to unmount the filesystem before any more damage is done.
  ZX_ASSERT(inode->header.IsInode() && inode->header.IsAllocated());

  TRACE_DURATION("blobfs", "BlobLoader::LoadBlob", "blob_size", inode->blob_size);

  const uint64_t num_data_blocks = BlobDataBlocks(*inode);
  if (num_data_blocks == 0) {
    // No data to load for the null blob.
    return ZX_OK;
  }

  fzl::OwnedVmoMapper merkle_mapper;
  std::unique_ptr<BlobVerifier> verifier;
  zx_status_t status;
  if ((status = InitMerkleVerifier(node_index, *inode, &merkle_mapper, &verifier)) != ZX_OK) {
    return status;
  }

  size_t data_vmo_size;
  if (mul_overflow(num_data_blocks, kBlobfsBlockSize, &data_vmo_size)) {
    FS_TRACE_ERROR("Multiplication overflow");
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::StringBuffer<ZX_MAX_NAME_LEN> data_vmo_name;
  FormatBlobDataVmoName(node_index, &data_vmo_name);

  fzl::OwnedVmoMapper data_mapper;
  status = data_mapper.CreateAndMap(data_vmo_size, data_vmo_name.c_str());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to initialize data vmo; error: %s\n",
                   zx_status_get_string(status));
    return status;
  }
  status = inode->IsCompressed()
      ? LoadAndDecompressData(node_index, *inode, data_mapper)
      : LoadData(node_index, *inode, data_mapper);
  if (status != ZX_OK) {
    return status;
  }

  if ((status = verifier->Verify(data_mapper.start(), inode->blob_size)) != ZX_OK) {
    return status;
  }

  *data_out = std::move(data_mapper);
  if (merkle_mapper.vmo().is_valid()) {
    *merkle_out = std::move(merkle_mapper);
  }
  return ZX_OK;
}

zx_status_t BlobLoader::LoadBlobPaged(uint32_t node_index,
                                      std::unique_ptr<PageWatcher>* page_watcher_out,
                                      fzl::OwnedVmoMapper* data_out,
                                      fzl::OwnedVmoMapper* merkle_out) {
  const Inode* const inode = blobfs_->GetNode(node_index);
  // LoadBlobPaged should only be called for Inodes. If this doesn't hold, one of two things
  // happened:
  //   - Programmer error
  //   - Corruption of a blob's Inode
  // In either case it is preferable to ASSERT than to return an error here, since the first case
  // should happen only during development and in the second case there may be more corruption and
  // we want to unmount the filesystem before any more damage is done.
  ZX_ASSERT(inode->header.IsInode() && inode->header.IsAllocated());

  TRACE_DURATION("blobfs", "BlobLoader::LoadBlobPaged", "blob_size", inode->blob_size);

  const uint64_t num_data_blocks = BlobDataBlocks(*inode);
  if (num_data_blocks == 0) {
    // No data to load for the null blob.
    return ZX_OK;
  }

  fzl::OwnedVmoMapper merkle_mapper;
  std::unique_ptr<BlobVerifier> verifier;
  zx_status_t status;
  if ((status = InitMerkleVerifier(node_index, *inode, &merkle_mapper, &verifier)) != ZX_OK) {
    return status;
  }

  UserPagerInfo userpager_info;
  userpager_info.identifier = node_index;
  userpager_info.data_start_bytes = ComputeNumMerkleTreeBlocks(*inode) * kBlobfsBlockSize;
  userpager_info.data_length_bytes = inode->blob_size;
  userpager_info.verifier = std::move(verifier);
  auto page_watcher = std::make_unique<PageWatcher>(pager_, std::move(userpager_info));

  fbl::StringBuffer<ZX_MAX_NAME_LEN> data_vmo_name;
  FormatBlobDataVmoName(node_index, &data_vmo_name);

  zx::vmo data_vmo;
  size_t data_vmo_size;
  if (mul_overflow(num_data_blocks, kBlobfsBlockSize, &data_vmo_size)) {
    FS_TRACE_ERROR("Multiplication overflow");
    return ZX_ERR_OUT_OF_RANGE;
  }
  if ((status = page_watcher->CreatePagedVmo(data_vmo_size, &data_vmo)) != ZX_OK) {
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
                                           fzl::OwnedVmoMapper* out_vmo,
                                           std::unique_ptr<BlobVerifier>* out_verifier) {
  uint64_t num_merkle_blocks = ComputeNumMerkleTreeBlocks(inode);
  if (num_merkle_blocks == 0) {
    return BlobVerifier::CreateWithoutTree(digest::Digest(inode.merkle_root_hash),
                                           blobfs_->Metrics(), inode.blob_size, out_verifier);
  }

  fzl::OwnedVmoMapper merkle_mapper;
  std::unique_ptr<BlobVerifier> verifier;

  size_t merkle_vmo_size;
  if (mul_overflow(num_merkle_blocks, kBlobfsBlockSize, &merkle_vmo_size)) {
    FS_TRACE_ERROR("Multiplication overflow");
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::StringBuffer<ZX_MAX_NAME_LEN> merkle_vmo_name;
  FormatBlobMerkleVmoName(node_index, &merkle_vmo_name);

  zx_status_t status;
  if ((status = merkle_mapper.CreateAndMap(merkle_vmo_size, merkle_vmo_name.c_str())) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to initialize merkle vmo; error: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  if ((status = LoadMerkle(node_index, inode, merkle_mapper)) != ZX_OK) {
    return status;
  }

  if ((status = BlobVerifier::Create(digest::Digest(inode.merkle_root_hash), blobfs_->Metrics(),
                                     merkle_mapper.start(), merkle_vmo_size, inode.blob_size,
                                     &verifier)) != ZX_OK) {
    return status;
  }

  *out_vmo = std::move(merkle_mapper);
  *out_verifier = std::move(verifier);
  return ZX_OK;
}

zx_status_t BlobLoader::LoadMerkle(uint32_t node_index, const Inode& inode,
                                   const fzl::OwnedVmoMapper& vmo) const {
  storage::OwnedVmoid vmoid(blobfs_);
  zx_status_t status;
  if ((status = vmoid.AttachVmo(vmo.vmo())) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to attach VMO to block device; error: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  uint32_t merkle_blocks = ComputeNumMerkleTreeBlocks(inode);
  uint64_t merkle_size = static_cast<uint64_t>(merkle_blocks) * kBlobfsBlockSize;

  TRACE_DURATION("blobfs", "BlobLoader::LoadMerkle", "merkle_size", merkle_size);
  fs::Ticker ticker(blobfs_->Metrics()->Collecting());
  fs::ReadTxn txn(blobfs_);

  const uint64_t kDataStart = DataStartBlock(blobfs_->Info());
  BlockIterator block_iter = blobfs_->BlockIteratorByNodeIndex(node_index);
  status = StreamBlocks(&block_iter, merkle_blocks,
                        [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
                          txn.Enqueue(vmoid.get(), vmo_offset, kDataStart + dev_offset,
                                      length);
                          return ZX_OK;
                        });
  if (status != ZX_OK) {
    return status;
  }

  if ((status = txn.Transact()) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to flush merkle read transaction: %d\n", status);
    return status;
  }

  blobfs_->Metrics()->UpdateMerkleDiskRead(merkle_size, ticker.End());
  return ZX_OK;
}

zx_status_t BlobLoader::LoadData(uint32_t node_index, const Inode& inode,
                                 const fzl::OwnedVmoMapper& vmo) const {
  TRACE_DURATION("blobfs", "BlobLoader::LoadData");

  zx_status_t status;
  fs::Duration read_duration;
  uint64_t bytes_read;
  if ((status = LoadDataInternal(node_index, inode, vmo, &read_duration, &bytes_read)) != ZX_OK) {
    return status;
  }
  blobfs_->Metrics()->UpdateMerkleDiskRead(bytes_read, read_duration);
  return ZX_OK;
}

zx_status_t BlobLoader::LoadAndDecompressData(uint32_t node_index,
                                              const Inode& inode,
                                              const fzl::OwnedVmoMapper& vmo) const {
  CompressionAlgorithm algorithm;
  if (inode.header.flags & kBlobFlagLZ4Compressed) {
    algorithm = CompressionAlgorithm::LZ4;
  } else if (inode.header.flags & kBlobFlagZSTDCompressed) {
    algorithm = CompressionAlgorithm::ZSTD;
  } else if (inode.header.flags & kBlobFlagZSTDSeekableCompressed) {
    algorithm = CompressionAlgorithm::ZSTD_SEEKABLE;
  } else {
    FS_TRACE_ERROR("Blob has no known compression format\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  const uint32_t num_merkle_blocks = ComputeNumMerkleTreeBlocks(inode);
  const uint32_t num_data_blocks = inode.block_count - num_merkle_blocks;
  size_t compressed_size;
  if (mul_overflow(num_data_blocks, kBlobfsBlockSize, &compressed_size)) {
    FS_TRACE_ERROR("Multiplication overflow\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  TRACE_DURATION("blobfs", "BlobLoader::LoadAndDecompressData", "compressed_size", compressed_size,
                 "blob_size", inode.blob_size);

  // Create and attach a transfer VMO for fetching the compressed contents from the block FIFO.
  fbl::StringBuffer<ZX_MAX_NAME_LEN> vmo_name;
  FormatBlobCompressedVmoName(node_index, &vmo_name);
  fzl::OwnedVmoMapper compressed_mapper;
  zx_status_t status = compressed_mapper.CreateAndMap(compressed_size, vmo_name.c_str());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to initialized compressed vmo; error: %d\n", status);
    return status;
  }

  fs::Duration read_duration;
  uint64_t bytes_read;
  if ((status = LoadDataInternal(node_index, inode, compressed_mapper, &read_duration,
                                 &bytes_read)) != ZX_OK) {
    return status;
  }

  fs::Ticker ticker(blobfs_->Metrics()->Collecting());

  // Decompress into the target buffer.
  size_t target_size = inode.blob_size;
  const void* compressed_buffer = compressed_mapper.start();
  std::unique_ptr<Decompressor> decompressor;
  status = Decompressor::Create(algorithm, &decompressor);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to create decompressor, status=%d", status);
    return status;
  }

  status = decompressor->Decompress(vmo.start(), &target_size, compressed_buffer, compressed_size);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to decompress data: %s\n", zx_status_get_string(status));
    return status;
  } else if (target_size != inode.blob_size) {
    FS_TRACE_ERROR("Failed to fully decompress blob (%zu of %zu expected)\n", target_size,
                   inode.blob_size);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  blobfs_->Metrics()->UpdateMerkleDecompress(compressed_size, inode.blob_size, read_duration,
                                             ticker.End());

  return ZX_OK;
}

zx_status_t BlobLoader::LoadDataInternal(uint32_t node_index, const Inode& inode,
                                         const fzl::OwnedVmoMapper& vmo,
                                         fs::Duration* out_duration,
                                         uint64_t* out_bytes_read) const {
  TRACE_DURATION("blobfs", "BlobLoader::LoadDataInternal");
  fs::Ticker ticker(blobfs_->Metrics()->Collecting());

  zx_status_t status;
  // Attach |vmo| for transfer to the block FIFO.
  storage::OwnedVmoid vmoid(blobfs_);
  if ((status = vmoid.AttachVmo(vmo.vmo())) != ZX_OK) {
    FS_TRACE_ERROR("Failed to attach VMO to block device; error: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  fs::ReadTxn txn(blobfs_);

  // Stream the blocks, skipping the first |merkle_blocks| which contain the merkle tree.
  uint32_t merkle_blocks = ComputeNumMerkleTreeBlocks(inode);
  uint32_t data_blocks = inode.block_count - merkle_blocks;
  const uint64_t kDataStart = DataStartBlock(blobfs_->Info());
  BlockIterator block_iter = blobfs_->BlockIteratorByNodeIndex(node_index);
  if ((status = IterateToBlock(&block_iter, merkle_blocks)) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to seek past merkle blocks: %s\n", zx_status_get_string(status));
    return status;
  }

  status = StreamBlocks(&block_iter, data_blocks,
                        [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
                          txn.Enqueue(vmoid.get(), vmo_offset - merkle_blocks,
                                      kDataStart + dev_offset, length);
                          return ZX_OK;
                        });
  if (status != ZX_OK) {
    return status;
  }
  if ((status = txn.Transact()) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to flush data read transaction: %d\n", status);
    return status;
  }

  *out_duration = ticker.End();
  *out_bytes_read = data_blocks * kBlobfsBlockSize;

  return ZX_OK;
}

}  // namespace blobfs
