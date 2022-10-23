// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blob_loader.h"

#include <lib/fit/defer.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/result.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <fbl/string_buffer.h>
#include <safemath/checked_math.h>
#include <storage/buffer/owned_vmoid.h>

#include "src/lib/digest/digest.h"
#include "src/lib/storage/vfs/cpp/trace.h"
#include "src/lib/storage/vfs/cpp/transaction/buffered_operations_builder.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/blob_verifier.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression/decompressor.h"
#include "src/storage/blobfs/compression/seekable_decompressor.h"
#include "src/storage/blobfs/compression_settings.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/iterator/block_iterator.h"
#include "src/storage/blobfs/transfer_buffer.h"
#include "storage/operation/operation.h"

namespace blobfs {

namespace {

// TODO(jfsulliv): Rationalize this with the size limits for chunk-compression headers.
constexpr uint64_t kChunkedHeaderSize = 4 * kBlobfsBlockSize;

}  // namespace

BlobLoader::BlobLoader(TransactionManager* txn_manager, BlockIteratorProvider* block_iter_provider,
                       NodeFinder* node_finder, std::shared_ptr<BlobfsMetrics> metrics,
                       storage::ResizeableVmoBuffer read_mapper, zx::vmo sandbox_vmo,
                       std::unique_ptr<ExternalDecompressorClient> decompressor_client)
    : txn_manager_(txn_manager),
      block_iter_provider_(block_iter_provider),
      node_finder_(node_finder),
      metrics_(std::move(metrics)),
      read_mapper_(std::move(read_mapper)),
      sandbox_vmo_(std::move(sandbox_vmo)),
      decompressor_client_(std::move(decompressor_client)) {}

BlobLoader::~BlobLoader() { [[maybe_unused]] auto status = read_mapper_.Detach(txn_manager_); }

zx::result<std::unique_ptr<BlobLoader>> BlobLoader::Create(
    TransactionManager* txn_manager, BlockIteratorProvider* block_iter_provider,
    NodeFinder* node_finder, std::shared_ptr<BlobfsMetrics> metrics,
    DecompressorCreatorConnector* decompression_connector) {
  auto read_mapper = storage::ResizeableVmoBuffer(txn_manager->Info().block_size);
  if (auto status = read_mapper.Attach("blobfs-loader", txn_manager); status.is_error()) {
    FX_LOGS(ERROR) << "blobfs: Failed to attach read vmo: " << status.status_string();
    return status.take_error();
  }
  zx::vmo sandbox_vmo;
  std::unique_ptr<ExternalDecompressorClient> decompressor_client = nullptr;
  if (decompression_connector) {
    if (auto status = zx::vmo::create(kDecompressionBufferSize, 0, &sandbox_vmo); status != ZX_OK) {
      return zx::error(status);
    }
    const char* name = "blobfs-sandbox";
    sandbox_vmo.set_property(ZX_PROP_NAME, name, strlen(name));
    zx::result<std::unique_ptr<ExternalDecompressorClient>> client_or =
        ExternalDecompressorClient::Create(decompression_connector, sandbox_vmo, read_mapper.vmo());
    if (!client_or.is_ok()) {
      return client_or.take_error();
    } else {
      decompressor_client = std::move(client_or.value());
    }
  }
  return zx::ok(std::unique_ptr<BlobLoader>(new BlobLoader(
      txn_manager, block_iter_provider, node_finder, std::move(metrics), std::move(read_mapper),
      std::move(sandbox_vmo), std::move(decompressor_client))));
}

zx::result<LoaderInfo> BlobLoader::LoadBlob(uint32_t node_index,
                                            const BlobCorruptionNotifier* corruption_notifier) {
  ZX_DEBUG_ASSERT(read_mapper_.vmo().is_valid());
  auto inode = node_finder_->GetNode(node_index);
  if (inode.is_error()) {
    return inode.take_error();
  }

  // LoadBlob should only be called for nonempty Inodes. If this doesn't hold, one of two things
  // happened:
  //   - Programmer error
  //   - Corruption of a blob's Inode
  // In either case it is preferable to ASSERT than to return an error here, since the first case
  // should happen only during development and in the second case there may be more corruption and
  // we want to unmount the filesystem before any more damage is done.
  ZX_ASSERT_MSG(inode->header.IsInode() && inode->header.IsAllocated(),
                "LoadBlob failed as inode->header.IsInode():%u inode->header.IsAllocated():%u",
                inode->header.IsInode(), inode->header.IsAllocated());
  ZX_ASSERT_MSG(inode->blob_size > 0, "Inode blob size should be greater than zero: %lu",
                inode->blob_size);
  TRACE_DURATION("blobfs", "BlobLoader::LoadBlob", "blob_size", inode->blob_size);

  // Create and save the layout.
  auto blob_layout_or = BlobLayout::CreateFromInode(GetBlobLayoutFormat(txn_manager_->Info()),
                                                    *inode.value(), GetBlockSize());
  if (blob_layout_or.is_error()) {
    FX_LOGS(ERROR) << "Failed to create blob layout: "
                   << zx_status_get_string(blob_layout_or.error_value());
    return blob_layout_or.take_error();
  }
  ZX_ASSERT(blob_layout_or->FileSize() == inode->blob_size);

  LoaderInfo result;
  result.node_index = node_index;
  result.layout = std::move(blob_layout_or.value());

  auto decommit_used = fit::defer([this] { Decommit(); });

  auto verifier_or =
      CreateBlobVerifier(node_index, *inode.value(), *result.layout, corruption_notifier);
  if (verifier_or.is_error())
    return verifier_or.take_error();
  result.verifier = std::move(verifier_or.value());

  if (zx_status_t status = InitForDecompression(node_index, *inode.value(), *result.layout,
                                                *result.verifier, &result.decompressor);
      status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(std::move(result));
}

zx::result<std::unique_ptr<BlobVerifier>> BlobLoader::CreateBlobVerifier(
    uint32_t node_index, const Inode& inode, const BlobLayout& blob_layout,
    const BlobCorruptionNotifier* notifier) {
  if (blob_layout.MerkleTreeSize() == 0) {
    return BlobVerifier::CreateWithoutTree(digest::Digest(inode.merkle_root_hash), metrics_,
                                           inode.blob_size, notifier);
  }

  std::unique_ptr<BlobVerifier> verifier;

  if (auto blocks = LoadBlocks(node_index, blob_layout.MerkleTreeBlockOffset(),
                               blob_layout.MerkleTreeBlockCount());
      blocks.is_error()) {
    FX_LOGS(ERROR) << "Failed to load Merkle tree: " << blocks.status_string();
    return blocks.take_error();
  } else {
    return BlobVerifier::Create(digest::Digest(inode.merkle_root_hash), metrics_, *blocks,
                                blob_layout, notifier);
  }
}

zx_status_t BlobLoader::InitForDecompression(
    uint32_t node_index, const Inode& inode, const BlobLayout& blob_layout,
    const BlobVerifier& verifier, std::unique_ptr<SeekableDecompressor>* decompressor_out) {
  zx::result<CompressionAlgorithm> algorithm_status = AlgorithmForInode(inode);
  if (algorithm_status.is_error()) {
    FX_LOGS(ERROR) << "Cannot decode blob due to invalid compression flags.";
    return algorithm_status.status_value();
  }

  if (algorithm_status.value() == CompressionAlgorithm::kUncompressed)
    return ZX_OK;

  TRACE_DURATION("blobfs", "BlobLoader::InitDecompressor");

  // The first few blocks of data contain the seek table, which we need to read to initialize the
  // decompressor. Read these from disk.

  // We don't know exactly how long the header is, so we generally overshoot.
  // (The header should never be bigger than the size of the kChunkedHeaderSize.)
  ZX_DEBUG_ASSERT(kChunkedHeaderSize % GetBlockSize() == 0);
  uint64_t header_block_count = kChunkedHeaderSize / GetBlockSize();
  uint64_t blocks_to_read = std::min(header_block_count, blob_layout.DataBlockCount());
  if (blocks_to_read == 0) {
    FX_LOGS(ERROR) << "No data blocks; corrupted inode?";
    return ZX_ERR_BAD_STATE;
  }

  cpp20::span<const uint8_t> bytes;
  if (auto bytes_or = LoadBlocks(node_index, blob_layout.DataBlockOffset(), blocks_to_read);
      bytes_or.is_error()) {
    FX_LOGS(ERROR) << "Failed to load compression header: " << bytes_or.status_string();
    return bytes_or.error_value();
  } else {
    uint64_t max_data_size = blob_layout.DataSizeUpperBound();
    if (bytes_or->size() > max_data_size) {
      bytes = bytes_or->subspan(0, max_data_size);
    } else {
      bytes = *bytes_or;
    }
  }

  if (zx_status_t status = SeekableChunkedDecompressor::CreateDecompressor(
          bytes,
          /*max_compressed_size=*/blob_layout.DataSizeUpperBound(), decompressor_out);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to init decompressor: " << zx_status_get_string(status);
    return status;
  }

  return ZX_OK;
}

zx::result<cpp20::span<const uint8_t>> BlobLoader::LoadBlocks(uint32_t node_index,
                                                              uint64_t block_offset,
                                                              uint64_t block_count) {
  TRACE_DURATION("blobfs", "BlobLoader::LoadBlocks", "block_count", block_count);

  if (block_count > read_mapper_.capacity()) {
    if (auto status = read_mapper_.Grow(block_count); status.is_error()) {
      FX_LOGS(ERROR) << "Failed to grow buffer: " << status.status_string();
      return status.take_error();
    }
  }

  zx_status_t status;

  const uint64_t kDataStart = DataStartBlock(txn_manager_->Info());
  auto block_iter = block_iter_provider_->BlockIteratorByNodeIndex(node_index);
  if (block_iter.is_error()) {
    return block_iter.take_error();
  }
  if ((status = IterateToBlock(&block_iter.value(), block_offset)) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to seek to starting block: " << zx_status_get_string(status);
    return zx::error(status);
  }
  std::vector<storage::BufferedOperation> operations;

  status = StreamBlocks(&block_iter.value(), block_count,
                        [&](uint64_t vmo_offset, uint64_t dev_offset, uint64_t length) {
                          operations.push_back({.vmoid = read_mapper_.GetHandle(),
                                                .op = {
                                                    .type = storage::OperationType::kRead,
                                                    .vmo_offset = vmo_offset - block_offset,
                                                    .dev_offset = kDataStart + dev_offset,
                                                    .length = length,
                                                }});
                          return ZX_OK;
                        });

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to stream blocks: " << zx_status_get_string(status);
    return zx::error(status);
  }
  status = txn_manager_->RunRequests(operations);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to flush read transaction: " << zx_status_get_string(status);
    return zx::error(status);
  }

  return zx::ok(
      cpp20::span(static_cast<const uint8_t*>(read_mapper_.Data(0)), block_count * GetBlockSize()));
}

uint64_t BlobLoader::GetBlockSize() const { return txn_manager_->Info().block_size; }

void BlobLoader::Decommit() {
  // Ignore errors.
  [[maybe_unused]] zx_status_t status = read_mapper_.vmo().op_range(
      ZX_VMO_OP_DECOMMIT, 0, read_mapper_.capacity() * GetBlockSize(), nullptr, 0);
}

}  // namespace blobfs
