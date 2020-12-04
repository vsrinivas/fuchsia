// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blob.h"

#include <assert.h>
#include <ctype.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fit/defer.h>
#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/status.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zircon/assert.h>
#include <zircon/device/vfs.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <blobfs/blob-layout.h>
#include <blobfs/common.h>
#include <blobfs/compression-settings.h>
#include <blobfs/format.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <digest/node-digest.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>
#include <fbl/span.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fs/journal/data_streamer.h>
#include <fs/metrics/events.h>
#include <fs/transaction/writeback.h>
#include <fs/vfs_types.h>
#include <safemath/checked_math.h>

#include "src/storage/blobfs/blob-verifier.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/iterator/allocated-extent-iterator.h"
#include "src/storage/blobfs/iterator/block-iterator.h"
#include "src/storage/blobfs/iterator/extent-iterator.h"
#include "src/storage/blobfs/iterator/node-populator.h"
#include "src/storage/blobfs/iterator/vector-extent-iterator.h"
#include "src/storage/blobfs/metrics.h"

namespace blobfs {

// Producer is an abstact class that is used when writing blobs.  It produces data (see the Consume
// method) which is then to be written to the device.
class Producer {
 public:
  Producer() = default;

  // The number of bytes remaining for this producer.
  virtual uint64_t remaining() const = 0;

  // Producers must be able to accommodate zero padding up to kBlobfsBlockSize if it would be
  // required i.e. if the last span returned is not a whole block size, it must point to a buffer
  // that can be extended with zero padding (which will be done by the caller).
  virtual zx::status<fbl::Span<const uint8_t>> Consume(uint64_t max) = 0;

  // Subclasses should return true if the next call to Consume would invalidate data returned by
  // previous calls to Consume.
  virtual bool NeedsFlush() const { return false; }

 protected:
  Producer(Producer&&) = default;
  Producer& operator=(Producer&&) = default;
};

namespace {

using ::digest::Digest;
using ::digest::MerkleTreeCreator;

bool SupportsPaging(const Inode& inode) {
  zx::status<CompressionAlgorithm> status = AlgorithmForInode(inode);
  if (status.is_ok() && (status.value() == CompressionAlgorithm::UNCOMPRESSED ||
                         status.value() == CompressionAlgorithm::CHUNKED)) {
    return true;
  }
  return false;
}

// Merges two producers together with optional padding between them.  If there is padding, we
// require the second producer to be able to accomodate padding at the beginning up to
// kBlobfsBlockSize i.e. the first span it returns must point to a buffer that can be prepended with
// up to kBlobfsBlockSize bytes.  Both producers should be able to accommodate padding at the end if
// it would be required.
class MergeProducer : public Producer {
 public:
  MergeProducer(Producer& first, Producer& second, size_t padding)
      : first_(first), second_(second), padding_(padding) {
    ZX_ASSERT(padding_ < kBlobfsBlockSize);
  }

  uint64_t remaining() const override {
    return first_.remaining() + padding_ + second_.remaining();
  }

  zx::status<fbl::Span<const uint8_t>> Consume(uint64_t max) override {
    if (first_.remaining() > 0) {
      auto data = first_.Consume(max);
      if (data.is_error()) {
        return data;
      }

      // Deal with data returned that isn't a multiple of the block size.
      const size_t alignment = data->size() % kBlobfsBlockSize;
      if (alignment > 0) {
        // First, add any padding that might be required.
        const size_t to_pad = std::min(padding_, kBlobfsBlockSize - alignment);
        uint8_t* p = const_cast<uint8_t*>(data->end());
        memset(p, 0, to_pad);
        p += to_pad;
        data.value() = fbl::Span(data->data(), data->size() + to_pad);
        padding_ -= to_pad;

        // If we still don't have a full block, fill the block with data from the second producer.
        const size_t alignment = data->size() % kBlobfsBlockSize;
        if (alignment > 0) {
          auto data2 = second_.Consume(kBlobfsBlockSize - alignment);
          if (data2.is_error())
            return data2;
          memcpy(p, data2->data(), data2->size());
          data.value() = fbl::Span(data->data(), data->size() + data2->size());
        }
      }
      return data;
    } else {
      auto data = second_.Consume(max - padding_);
      if (data.is_error())
        return data;

      // If we have some padding, prepend zeroed data.
      if (padding_ > 0) {
        data.value() = fbl::Span(data->data() - padding_, data->size() + padding_);
        memset(const_cast<uint8_t*>(data->data()), 0, padding_);
        padding_ = 0;
      }
      return data;
    }
  }

  bool NeedsFlush() const override { return first_.NeedsFlush() || second_.NeedsFlush(); }

 private:
  Producer& first_;
  Producer& second_;
  size_t padding_;
};

// A simple producer that just vends data from a supplied span.
class SimpleProducer : public Producer {
 public:
  SimpleProducer(fbl::Span<const uint8_t> data) : data_(data) {}

  uint64_t remaining() const override { return data_.size(); }

  zx::status<fbl::Span<const uint8_t>> Consume(uint64_t max) override {
    auto result = data_.subspan(0, std::min(max, data_.size()));
    data_ = data_.subspan(result.size());
    return zx::ok(result);
  }

 private:
  fbl::Span<const uint8_t> data_;
};

// A producer that allows us to write uncompressed data by decompressing data.  This is used when we
// discover that it is not profitable to compress a blob.  It decompresses into a temporary buffer.
class DecompressProducer : public Producer {
 public:
  static zx::status<DecompressProducer> Create(BlobCompressor& compressor,
                                               uint64_t decompressed_size) {
    ZX_ASSERT_MSG(compressor.algorithm() == CompressionAlgorithm::CHUNKED, "%u",
                  compressor.algorithm());
    std::unique_ptr<SeekableDecompressor> decompressor;
    const size_t compressed_size = compressor.Size();
    if (zx_status_t status = SeekableChunkedDecompressor::CreateDecompressor(
            compressor.Data(), compressed_size, compressed_size, &decompressor);
        status != ZX_OK) {
      return zx::error(status);
    }

    return zx::ok(DecompressProducer(
        std::move(decompressor), decompressed_size,
        fbl::round_up(131'072ul, compressor.compressor().GetChunkSize()), compressor.Data()));
  }

  uint64_t remaining() const override { return decompressed_remaining_ + buffer_avail_; }

  zx::status<fbl::Span<const uint8_t>> Consume(uint64_t max) override {
    if (buffer_avail_ == 0) {
      if (zx_status_t status = Decompress(); status != ZX_OK) {
        return zx::error(status);
      }
    }
    fbl::Span result(buffer_.get() + buffer_offset_, std::min(buffer_avail_, max));
    buffer_offset_ += result.size();
    buffer_avail_ -= result.size();
    return zx::ok(result);
  }

  // Return true if previous data would be invalidated by the next call to Consume.
  bool NeedsFlush() const override { return buffer_offset_ > 0 && buffer_avail_ == 0; }

 private:
  DecompressProducer(std::unique_ptr<SeekableDecompressor> decompressor, uint64_t decompressed_size,
                     size_t buffer_size, const void* compressed_data_start)
      : decompressor_(std::move(decompressor)),
        decompressed_remaining_(decompressed_size),
        buffer_(std::make_unique<uint8_t[]>(buffer_size)),
        buffer_size_(buffer_size),
        compressed_data_start_(static_cast<const uint8_t*>(compressed_data_start)) {}

  // Decompress into the temporary buffer.
  zx_status_t Decompress() {
    size_t decompressed_length = std::min(buffer_size_, decompressed_remaining_);
    auto mapping_or = decompressor_->MappingForDecompressedRange(
        decompressed_offset_, decompressed_length, std::numeric_limits<size_t>::max());
    if (mapping_or.is_error()) {
      return mapping_or.error_value();
    }
    ZX_ASSERT(mapping_or.value().decompressed_offset == decompressed_offset_);
    ZX_ASSERT(mapping_or.value().decompressed_length == decompressed_length);
    if (zx_status_t status = decompressor_->DecompressRange(
            buffer_.get(), &decompressed_length,
            compressed_data_start_ + mapping_or.value().compressed_offset,
            mapping_or.value().compressed_length, mapping_or.value().decompressed_offset);
        status != ZX_OK) {
      return status;
    }
    ZX_ASSERT(mapping_or.value().decompressed_length == decompressed_length);
    buffer_avail_ = decompressed_length;
    buffer_offset_ = 0;
    decompressed_remaining_ -= decompressed_length;
    decompressed_offset_ += decompressed_length;
    return ZX_OK;
  }

  std::unique_ptr<SeekableDecompressor> decompressor_;

  // The total number of decompressed bytes left to decompress.
  uint64_t decompressed_remaining_;

  // A temporary buffer we use to decompress into.
  std::unique_ptr<uint8_t[]> buffer_;

  // The size of the temporary buffer.
  const size_t buffer_size_;

  // Pointer to the first byte of compressed data.
  const uint8_t* compressed_data_start_;

  // The current offset of decompressed bytes.
  uint64_t decompressed_offset_ = 0;

  // The current offset in the temporary buffer indicate what to return on the next call to Consume.
  size_t buffer_offset_ = 0;

  // The number of bytes available in the temporary buffer.
  size_t buffer_avail_ = 0;
};

}  // namespace

zx_status_t Blob::Verify() const {
  if (inode_.blob_size > 0) {
    ZX_ASSERT(IsDataLoaded());
  }

  auto blob_layout =
      BlobLayout::CreateFromInode(GetBlobLayoutFormat(blobfs_->Info()), inode_, GetBlockSize());
  if (blob_layout.is_error()) {
    FX_LOGS(ERROR) << "Failed to create blob layout: " << blob_layout.status_string();
    return blob_layout.status_value();
  }

  zx_status_t status;
  std::unique_ptr<BlobVerifier> verifier;

  if (blob_layout->MerkleTreeSize() == 0) {
    // No merkle tree is stored for small blobs, because the entire blob can be verified based
    // on its merkle root digest (i.e. the blob's merkle tree is just a single root digest).
    // Still verify the blob's contents in this case.
    if ((status = BlobVerifier::CreateWithoutTree(
             MerkleRoot(), blobfs_->Metrics(), inode_.blob_size, blobfs_->GetCorruptBlobNotifier(),
             &verifier)) != ZX_OK) {
      return status;
    }
  } else {
    ZX_ASSERT(IsMerkleTreeLoaded());
    if ((status = BlobVerifier::Create(
             MerkleRoot(), blobfs_->Metrics(), GetMerkleTreeBuffer(*blob_layout.value()),
             blob_layout->MerkleTreeSize(), blob_layout->Format(), inode_.blob_size,
             blobfs_->GetCorruptBlobNotifier(), &verifier)) != ZX_OK) {
      return status;
    }
  }

  return verifier->Verify(data_mapping_.start(), inode_.blob_size, data_mapping_.size());
}

uint64_t Blob::SizeData() const {
  if (state() == BlobState::kReadable) {
    return inode_.blob_size;
  }
  return 0;
}

Blob::Blob(Blobfs* bs, const Digest& digest)
    : CacheNode(digest), blobfs_(bs), clone_watcher_(this) {}

Blob::Blob(Blobfs* bs, uint32_t node_index, const Inode& inode)
    : CacheNode(Digest(inode.merkle_root_hash)),
      blobfs_(bs),
      state_(BlobState::kReadable),
      syncing_state_(SyncingState::kDone),
      map_index_(node_index),
      clone_watcher_(this),
      inode_(inode) {}

zx_status_t Blob::WriteNullBlob() {
  ZX_DEBUG_ASSERT(inode_.blob_size == 0);
  ZX_DEBUG_ASSERT(inode_.block_count == 0);

  zx_status_t status = Verify();
  if (status != ZX_OK) {
    return status;
  }

  BlobTransaction transaction;
  if (zx_status_t status = WriteMetadata(transaction); status != ZX_OK) {
    return status;
  }
  transaction.Commit(*blobfs_->journal(), {},
                     [blob = fbl::RefPtr(this)]() { blob->CompleteSync(); });

  return MarkReadable();
}

zx_status_t Blob::PrepareWrite(uint64_t size_data, bool compress) {
  if (size_data > 0 && fbl::round_up(size_data, kBlobfsBlockSize) == 0) {
    // Fail early if |len| would overflow when rounded up to block size.
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (state() != BlobState::kEmpty) {
    return ZX_ERR_BAD_STATE;
  }

  memset(inode_.merkle_root_hash, 0, sizeof(inode_.merkle_root_hash));
  inode_.blob_size = size_data;

  auto write_info = std::make_unique<WriteInfo>();

  // Reserve a node for blob's inode. We might need more nodes for extents later.
  zx_status_t status = blobfs_->GetAllocator()->ReserveNodes(1, &write_info->node_indices);
  if (status != ZX_OK) {
    return status;
  }
  map_index_ = write_info->node_indices[0].index();

  // For compressed blobs, we only write into the compression buffer.  For uncompressed blobs we
  // write into the data vmo.
  if (compress) {
    write_info->compressor =
        BlobCompressor::Create(blobfs_->write_compression_settings(), inode_.blob_size);
    if (!write_info->compressor) {
      FX_LOGS(ERROR) << "Failed to initialize compressor: " << status;
      return status;
    }
  } else if (inode_.blob_size != 0) {
    if ((status = PrepareDataVmoForWriting()) != ZX_OK) {
      return status;
    }
  }

  write_info->merkle_tree_creator.SetUseCompactFormat(
      ShouldUseCompactMerkleTreeFormat(GetBlobLayoutFormat(blobfs_->Info())));
  if ((status = write_info->merkle_tree_creator.SetDataLength(inode_.blob_size)) != ZX_OK) {
    return status;
  }
  const size_t tree_len = write_info->merkle_tree_creator.GetTreeLength();
  // Allow for zero padding before and after.
  write_info->merkle_tree_buffer =
      std::make_unique<uint8_t[]>(tree_len + WriteInfo::kPreMerkleTreePadding);
  if ((status = write_info->merkle_tree_creator.SetTree(
           write_info->merkle_tree(), tree_len, &write_info->digest, sizeof(write_info->digest))) !=
      ZX_OK) {
    return status;
  }

  write_info_ = std::move(write_info);
  set_state(BlobState::kDataWrite);

  // Special case for the null blob: We skip the write phase.
  return inode_.blob_size == 0 ? WriteNullBlob() : ZX_OK;
}

zx_status_t Blob::SpaceAllocate(uint32_t block_count) {
  TRACE_DURATION("blobfs", "Blobfs::SpaceAllocate", "block_count", block_count);
  ZX_ASSERT(block_count != 0);

  fs::Ticker ticker(blobfs_->Metrics()->Collecting());

  // Initialize the inode with known fields. The block count may change if the
  // blob is compressible.
  inode_.block_count = block_count;

  fbl::Vector<ReservedExtent> extents;
  fbl::Vector<ReservedNode> nodes;

  // Reserve space for the blob.
  const uint64_t reserved_blocks = blobfs_->GetAllocator()->ReservedBlockCount();
  zx_status_t status = blobfs_->GetAllocator()->ReserveBlocks(inode_.block_count, &extents);
  if (status == ZX_ERR_NO_SPACE && reserved_blocks > 0) {
    // It's possible that a blob has just been unlinked but has yet to be flushed through the
    // journal, and the blocks are still reserved, so if that looks likely, force a flush and then
    // try again.  This might need to be revisited if/when blobfs becomes multi-threaded.
    sync_completion_t sync;
    blobfs_->Sync([&](zx_status_t) { sync_completion_signal(&sync); });
    sync_completion_wait(&sync, ZX_TIME_INFINITE);
    status = blobfs_->GetAllocator()->ReserveBlocks(inode_.block_count, &extents);
  }
  if (status != ZX_OK) {
    return status;
  }
  if (extents.size() > kMaxBlobExtents) {
    FX_LOGS(ERROR) << "Error: Block reservation requires too many extents (" << extents.size()
                   << " vs " << kMaxBlobExtents << " max)";
    return ZX_ERR_BAD_STATE;
  }
  const ExtentCountType extent_count = static_cast<ExtentCountType>(extents.size());

  // Reserve space for all additional nodes necessary to contain this blob.
  // The inode has already been reserved in Blob::PrepareWrite.
  // Hence, we need to reserve one less node here.
  size_t node_count = NodePopulator::NodeCountForExtents(extent_count) - 1;
  status = blobfs_->GetAllocator()->ReserveNodes(node_count, &nodes);
  if (status != ZX_OK) {
    return status;
  }

  write_info_->extents = std::move(extents);
  while (!nodes.is_empty()) {
    write_info_->node_indices.push_back(nodes.erase(0));
  }
  blobfs_->Metrics()->UpdateAllocation(inode_.blob_size, ticker.End());
  return ZX_OK;
}

bool Blob::IsDataLoaded() const { return data_mapping_.vmo().is_valid(); }
bool Blob::IsMerkleTreeLoaded() const { return merkle_mapping_.vmo().is_valid(); }

void* Blob::GetDataBuffer() const { return data_mapping_.start(); }
void* Blob::GetMerkleTreeBuffer(const BlobLayout& blob_layout) const {
  void* vmo_start = merkle_mapping_.start();
  if (vmo_start == nullptr) {
    return nullptr;
  }
  // In the compact format the Merkle tree doesn't start at the beginning of the VMO.
  return static_cast<uint8_t*>(vmo_start) + blob_layout.MerkleTreeOffsetWithinBlockOffset();
}

bool Blob::IsPagerBacked() const {
  return SupportsPaging(inode_) && state() == BlobState::kReadable;
}

Digest Blob::MerkleRoot() const { return GetKeyAsDigest(); }

zx_status_t Blob::WriteMetadata(BlobTransaction& transaction) {
  TRACE_DURATION("blobfs", "Blobfs::WriteMetadata");
  assert(state() == BlobState::kDataWrite);

  // Update the on-disk hash.
  MerkleRoot().CopyTo(inode_.merkle_root_hash);

  if (inode_.block_count) {
    // We utilize the NodePopulator class to take our reserved blocks and nodes and fill the
    // persistent map with an allocated inode / container.

    // If |on_node| is invoked on a node, it means that node was necessary to represent this
    // blob. Persist the node back to durable storage.
    auto on_node = [this, &transaction](uint32_t node_index) {
      blobfs_->PersistNode(node_index, transaction);
    };

    // If |on_extent| is invoked on an extent, it was necessary to represent this blob. Persist
    // the allocation of these blocks back to durable storage.
    //
    // Additionally, because of the compression feature of blobfs, it is possible we reserved
    // more extents than this blob ended up using. Decrement |remaining_blocks| to track if we
    // should exit early.
    size_t remaining_blocks = inode_.block_count;
    auto on_extent = [this, &transaction, &remaining_blocks](ReservedExtent& extent) {
      ZX_DEBUG_ASSERT(remaining_blocks > 0);
      if (remaining_blocks >= extent.extent().Length()) {
        // Consume the entire extent.
        remaining_blocks -= extent.extent().Length();
      } else {
        // Consume only part of the extent; we're done iterating.
        extent.SplitAt(static_cast<BlockCountType>(remaining_blocks));
        remaining_blocks = 0;
      }
      blobfs_->PersistBlocks(extent, transaction);
      if (remaining_blocks == 0) {
        return NodePopulator::IterationCommand::Stop;
      }
      return NodePopulator::IterationCommand::Continue;
    };

    InodePtr mapped_inode = blobfs_->GetNode(map_index_);
    *mapped_inode = inode_;
    NodePopulator populator(blobfs_->GetAllocator(), std::move(write_info_->extents),
                            std::move(write_info_->node_indices));
    ZX_ASSERT(populator.Walk(on_node, on_extent) == ZX_OK);

    // Ensure all non-allocation flags are propagated to the inode.
    const uint16_t non_allocation_flags = kBlobFlagMaskAnyCompression;
    {
      const uint16_t compression_flags = inode_.header.flags & kBlobFlagMaskAnyCompression;
      // Kernighan's algorithm for bit counting, returns 0 when zero or one bits are set.
      ZX_DEBUG_ASSERT((compression_flags & (compression_flags - 1)) == 0);
    }
    mapped_inode->header.flags &= ~non_allocation_flags;  // Clear any existing flags first.
    mapped_inode->header.flags |= (inode_.header.flags & non_allocation_flags);
  } else {
    // Special case: Empty node.
    ZX_DEBUG_ASSERT(write_info_->node_indices.size() == 1);
    *(blobfs_->GetNode(map_index_)) = inode_;
    blobfs_->GetAllocator()->MarkInodeAllocated(std::move(write_info_->node_indices[0]));
    blobfs_->PersistNode(map_index_, transaction);
  }
  return ZX_OK;
}

zx_status_t Blob::WriteInternal(const void* data, size_t len, size_t* actual) {
  TRACE_DURATION("blobfs", "Blobfs::WriteInternal", "data", data, "len", len);

  *actual = 0;
  if (len == 0) {
    return ZX_OK;
  }

  if (state() != BlobState::kDataWrite) {
    if (state() == BlobState::kError && write_info_ && write_info_->write_error != ZX_OK) {
      return write_info_->write_error;
    }
    return ZX_ERR_BAD_STATE;
  }

  const size_t to_write = std::min(len, inode_.blob_size - write_info_->bytes_written);
  const size_t offset = write_info_->bytes_written;

  *actual = to_write;
  write_info_->bytes_written += to_write;

  if (write_info_->compressor) {
    if (zx_status_t status = write_info_->compressor->Update(data, to_write); status != ZX_OK) {
      return status;
    }
  } else {
    if (zx_status_t status = data_mapping_.vmo().write(data, offset, to_write); status != ZX_OK) {
      FX_LOGS(ERROR) << "blob: VMO write failed: " << zx_status_get_string(status);
      return status;
    }
  }

  if (zx_status_t status = write_info_->merkle_tree_creator.Append(data, to_write);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "blob: MerkleTreeCreator::Append failed: " << zx_status_get_string(status);
    return status;
  }

  // More data to write.
  if (write_info_->bytes_written < inode_.blob_size) {
    return ZX_OK;
  }

  zx_status_t status = Commit();
  if (status != ZX_OK) {
    // Record the status so that if called again, we return the same status again.  This is done
    // because it's possible that the end-user managed to partially write some data to this blob in
    // which case the error could be dropped (by zxio or some other layer) and a short write
    // returned instead.  If this happens, the end-user will retry at which point it's helpful if we
    // return the same error rather than ZX_ERR_BAD_STATE (see above).
    write_info_->write_error = status;
    set_state(BlobState::kError);
  }

  return status;
}

zx_status_t Blob::Commit() {
  if (MerkleRoot() != write_info_->digest) {
    // Downloaded blob did not match provided digest.
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  const size_t merkle_size = write_info_->merkle_tree_creator.GetTreeLength();
  bool compress = write_info_->compressor.has_value();
  if (compress) {
    if (zx_status_t status = write_info_->compressor->End(); status != ZX_OK) {
      return status;
    }
    // If we're using the chunked compressor, abort compression if we're not going to get any
    // savings.  We can't easily do it for the other compression formats without changing the
    // decompression API to support streaming.
    if (write_info_->compressor->algorithm() == CompressionAlgorithm::CHUNKED &&
        fbl::round_up(write_info_->compressor->Size() + merkle_size, kBlobfsBlockSize) >=
            fbl::round_up(inode_.blob_size + merkle_size, kBlobfsBlockSize)) {
      compress = false;
    }
  }

  fs::Duration generation_time;

  const uint64_t data_size = compress ? write_info_->compressor->Size() : inode_.blob_size;
  auto blob_layout = BlobLayout::CreateFromSizes(GetBlobLayoutFormat(blobfs_->Info()),
                                                 inode_.blob_size, data_size, GetBlockSize());
  if (blob_layout.is_error()) {
    FX_LOGS(ERROR) << "Failed to create blob layout: " << blob_layout.status_string();
    return blob_layout.status_value();
  }

  const uint32_t total_block_count = blob_layout->TotalBlockCount();
  if (zx_status_t status = SpaceAllocate(total_block_count); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to allocate " << total_block_count
                   << " blocks for the blob: " << zx_status_get_string(status);
    return status;
  }

  std::variant<std::monostate, DecompressProducer, SimpleProducer> data;
  Producer* data_ptr = nullptr;

  if (compress) {
    // The data comes from the compression buffer.
    data_ptr = &data.emplace<SimpleProducer>(
        fbl::Span(static_cast<const uint8_t*>(write_info_->compressor->Data()),
                  write_info_->compressor->Size()));
    SetCompressionAlgorithm(&inode_, write_info_->compressor->algorithm());
  } else if (write_info_->compressor) {
    // In this case, we've decided against compressing because there are no savings, so we have to
    // decompress.
    if (auto producer_or = DecompressProducer::Create(*write_info_->compressor, inode_.blob_size);
        producer_or.is_error()) {
      return producer_or.error_value();
    } else {
      data_ptr = &data.emplace<DecompressProducer>(std::move(producer_or).value());
    }
  } else {
    // The data comes from the data buffer.
    data_ptr = &data.emplace<SimpleProducer>(
        fbl::Span(static_cast<const uint8_t*>(data_mapping_.start()), inode_.blob_size));
  }

  SimpleProducer merkle(fbl::Span(write_info_->merkle_tree(), merkle_size));

  MergeProducer producer = [&]() {
    switch (blob_layout->Format()) {
      case BlobLayoutFormat::kPaddedMerkleTreeAtStart:
        // Write the merkle data first followed by the data.  The merkle data should be a multiple
        // of the block size so we don't need any padding.
        ZX_ASSERT(merkle.remaining() % kBlobfsBlockSize == 0);
        return MergeProducer(merkle, *data_ptr, /*padding=*/0);
      case BlobLayoutFormat::kCompactMerkleTreeAtEnd:
        // Write the data followed by the merkle tree.  There might be some padding between the
        // data and the merkle tree.
        return MergeProducer(*data_ptr, merkle,
                             blob_layout->MerkleTreeOffset() - data_ptr->remaining());
    }
  }();

  fs::DataStreamer streamer(blobfs_->journal(), blobfs_->WriteBufferBlockCount());
  if (zx_status_t status = WriteData(total_block_count, producer, streamer); status != ZX_OK) {
    return status;
  }

  // No more data to write. Flush to disk.
  fs::Ticker ticker(blobfs_->Metrics()->Collecting());  // Tracking enqueue time.

  // Enqueue the blob's final data work. Metadata must be enqueued separately.
  zx_status_t data_status = ZX_ERR_IO;
  sync_completion_t data_written;
  // Issue the signal when the callback is destroyed rather than in the callback because the
  // callback won't get called in some error paths.
  auto data_written_finished = fit::defer([&] { sync_completion_signal(&data_written); });
  auto write_all_data = streamer.Flush().then(
      [&data_status, data_written_finished = std::move(data_written_finished)](
          const fit::result<void, zx_status_t>& result) {
        data_status = result.is_ok() ? ZX_OK : result.error();
        return result;
      });

  // Discard things we don't need any more.  This has to be after the Flush call above to ensure
  // all data has been copied from these buffers.
  data_mapping_.Reset();
  write_info_->compressor.reset();

  // Wrap all pending writes with a strong reference to this Blob, so that it stays
  // alive while there are writes in progress acting on it.
  BlobTransaction transaction;
  if (zx_status_t status = WriteMetadata(transaction); status != ZX_OK) {
    return status;
  }
  transaction.Commit(*blobfs_->journal(), std::move(write_all_data),
                     [self = fbl::RefPtr(this)]() {});

  // It's not safe to continue until all data has been written because we might need to reload it
  // (e.g. if the blob is immediately read after writing), and the journal caches data in ring
  // buffers, so wait until that has happened.  We don't need to wait for the metadata because we
  // cache that.
  sync_completion_wait(&data_written, ZX_TIME_INFINITE);
  if (data_status != ZX_OK) {
    return data_status;
  }

  blobfs_->Metrics()->UpdateClientWrite(inode_.block_count * kBlobfsBlockSize, merkle_size,
                                        ticker.End(), generation_time);
  return MarkReadable();
}

zx_status_t Blob::WriteData(uint32_t block_count, Producer& producer, fs::DataStreamer& streamer) {
  BlockIterator block_iter(std::make_unique<VectorExtentIterator>(write_info_->extents));
  const uint64_t data_start = DataStartBlock(blobfs_->Info());
  return StreamBlocks(
      &block_iter, block_count,
      [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t block_count) {
        while (block_count) {
          if (producer.NeedsFlush()) {
            // Queued operations might point at buffers that are about to be invalidated, so we have
            // to force those operations to be issued which will cause them to be copied.
            streamer.IssueOperations();
          }
          auto data = producer.Consume(block_count * kBlobfsBlockSize);
          if (data.is_error())
            return data.error_value();
          ZX_ASSERT(!data->empty());
          storage::UnbufferedOperation op = {.data = data->data(),
                                             .op = {
                                                 .type = storage::OperationType::kWrite,
                                                 .dev_offset = dev_offset + data_start,
                                                 .length = data->size() / kBlobfsBlockSize,
                                             }};
          // Pad if necessary.
          const size_t alignment = data->size() % kBlobfsBlockSize;
          if (alignment > 0) {
            memset(const_cast<uint8_t*>(data->end()), 0, kBlobfsBlockSize - alignment);
            ++op.op.length;
          }
          block_count -= op.op.length;
          dev_offset += op.op.length;
          streamer.StreamData(std::move(op));
        }  // while (block_count)
        return ZX_OK;
      });
}

zx_status_t Blob::MarkReadable() {
  if (readable_event_.is_valid()) {
    zx_status_t status = readable_event_.signal(0u, ZX_USER_SIGNAL_0);
    if (status != ZX_OK) {
      set_state(BlobState::kError);
      return status;
    }
  }
  set_state(BlobState::kReadable);
  {
    std::scoped_lock guard(mutex_);
    syncing_state_ = SyncingState::kSyncing;
  }
  write_info_.reset();
  return ZX_OK;
}

zx_status_t Blob::GetReadableEvent(zx::event* out) {
  TRACE_DURATION("blobfs", "Blobfs::GetReadableEvent");
  zx_status_t status;
  // This is the first 'wait until read event' request received.
  if (!readable_event_.is_valid()) {
    status = zx::event::create(0, &readable_event_);
    if (status != ZX_OK) {
      return status;
    } else if (state() == BlobState::kReadable) {
      readable_event_.signal(0u, ZX_USER_SIGNAL_0);
    }
  }
  zx::event out_event;
  status = readable_event_.duplicate(ZX_RIGHTS_BASIC, &out_event);
  if (status != ZX_OK) {
    return status;
  }

  *out = std::move(out_event);
  return ZX_OK;
}

zx_status_t Blob::CloneDataVmo(zx_rights_t rights, zx::vmo* out_vmo, size_t* out_size) {
  TRACE_DURATION("blobfs", "Blobfs::CloneVmo", "rights", rights);
  if (state() != BlobState::kReadable) {
    return ZX_ERR_BAD_STATE;
  }
  if (inode_.blob_size == 0) {
    return ZX_ERR_BAD_STATE;
  }

  auto status = LoadVmosFromDisk();
  if (status != ZX_OK) {
    return status;
  }
  const zx::vmo& data_vmo = data_mapping_.vmo();

  zx::vmo clone;
  status = data_vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, inode_.blob_size, &clone);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create child VMO: " << zx_status_get_string(status);
    return status;
  }

  // Only add exec right to VMO if explictly requested.  (Saves a syscall if
  // we're just going to drop the right back again in replace() call below.)
  if (rights & ZX_RIGHT_EXECUTE) {
    // Check if the VMEX resource held by Blobfs is valid and fail if it isn't. We do this to make
    // sure that we aren't implicitly relying on the ZX_POL_AMBIENT_MARK_VMO_EXEC job policy.
    const zx::resource& vmex = blobfs_->vmex_resource();
    if (!vmex.is_valid()) {
      FX_LOGS(ERROR) << "No VMEX resource available, executable blobs unsupported";
      return ZX_ERR_NOT_SUPPORTED;
    }
    if ((status = clone.replace_as_executable(vmex, &clone)) != ZX_OK) {
      return status;
    }
  }

  // Narrow rights to those requested.
  if ((status = clone.replace(rights, &clone)) != ZX_OK) {
    return status;
  }
  *out_vmo = std::move(clone);
  *out_size = inode_.blob_size;

  if (clone_watcher_.object() == ZX_HANDLE_INVALID) {
    clone_watcher_.set_object(data_vmo.get());
    clone_watcher_.set_trigger(ZX_VMO_ZERO_CHILDREN);

    // Keep a reference to "this" alive, preventing the blob
    // from being closed while someone may still be using the
    // underlying memory.
    //
    // We'll release it when no client-held VMOs are in use.
    clone_ref_ = fbl::RefPtr<Blob>(this);
    clone_watcher_.Begin(blobfs_->dispatcher());
  }

  return ZX_OK;
}

void Blob::HandleNoClones(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal) {
  const zx::vmo& vmo = data_mapping_.vmo();
  if (vmo.is_valid()) {
    zx_info_vmo_t info;
    zx_status_t info_status = vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr);
    if (info_status == ZX_OK) {
      if (info.num_children > 0) {
        // A clone was added at some point since the asynchronous HandleNoClones call was enqueued.
        // Re-arm the watcher, and return.
        //
        // clone_watcher_ is level triggered, so even if there are no clones now (since the call to
        // get_info), HandleNoClones will still be enqueued.
        //
        // No new clones can be added during this function, since clones are added on the main
        // dispatch thread which is currently running this function. If blobfs becomes
        // multi-threaded, locking will be necessary here.
        clone_watcher_.set_object(vmo.get());
        clone_watcher_.set_trigger(ZX_VMO_ZERO_CHILDREN);
        clone_watcher_.Begin(blobfs_->dispatcher());
        return;
      }
    } else {
      FX_LOGS(WARNING) << "Failed to get_info for vmo (" << zx_status_get_string(info_status)
                       << "); unable to verify VMO has no clones.";
    }
  }
  if (!tearing_down_) {
    ZX_DEBUG_ASSERT(status == ZX_OK);
    ZX_DEBUG_ASSERT((signal->observed & ZX_VMO_ZERO_CHILDREN) != 0);
    ZX_DEBUG_ASSERT(clone_watcher_.object() != ZX_HANDLE_INVALID);
  }
  clone_watcher_.set_object(ZX_HANDLE_INVALID);
  clone_ref_ = nullptr;
}

zx_status_t Blob::ReadInternal(void* data, size_t len, size_t off, size_t* actual) {
  TRACE_DURATION("blobfs", "Blobfs::ReadInternal", "len", len, "off", off);

  if (state() != BlobState::kReadable) {
    return ZX_ERR_BAD_STATE;
  }

  auto status = LoadVmosFromDisk();
  if (status != ZX_OK) {
    return status;
  }

  if (inode_.blob_size == 0) {
    *actual = 0;
    return ZX_OK;
  }
  if (off >= inode_.blob_size) {
    *actual = 0;
    return ZX_OK;
  }
  if (len > (inode_.blob_size - off)) {
    len = inode_.blob_size - off;
  }

  status = data_mapping_.vmo().read(data, off, len);
  if (status == ZX_OK) {
    *actual = len;
  }
  return status;
}

zx_status_t Blob::LoadVmosFromDisk() {
  if (IsDataLoaded()) {
    return ZX_OK;
  }
  BlobLoader& loader = blobfs_->loader();

  zx_status_t status;
  if (IsPagerBacked()) {
    // If there is an overriden cache policy for pager-backed blobs, apply it now.
    // Otherwise the system-wide default will be used.
    std::optional<CachePolicy> cache_policy = blobfs_->pager_backed_cache_policy();
    if (cache_policy) {
      set_overridden_cache_policy(*cache_policy);
    }
    status = loader.LoadBlobPaged(map_index_, blobfs_->GetCorruptBlobNotifier(), &page_watcher_,
                                  &data_mapping_, &merkle_mapping_);
  } else {
    status = loader.LoadBlob(map_index_, blobfs_->GetCorruptBlobNotifier(), &data_mapping_,
                             &merkle_mapping_);
  }

  std::scoped_lock guard(mutex_);
  syncing_state_ = SyncingState::kDone;  // Nothing to sync when blob was loaded from the device.
  return status;
}

zx_status_t Blob::PrepareDataVmoForWriting() {
  if (IsDataLoaded())
    return ZX_OK;
  fzl::OwnedVmoMapper data_mapping;
  fbl::StringBuffer<ZX_MAX_NAME_LEN> data_vmo_name;
  FormatBlobDataVmoName(Ino(), &data_vmo_name);
  if (zx_status_t status = data_mapping.CreateAndMap(
          fbl::round_up(inode_.blob_size, kBlobfsBlockSize), data_vmo_name.c_str());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to map data vmo: " << zx_status_get_string(status);
    return status;
  }
  data_mapping_ = std::move(data_mapping);
  return ZX_OK;
}

zx_status_t Blob::QueueUnlink() {
  deletable_ = true;
  // Attempt to purge in case the blob has been unlinked with no open fds
  return TryPurge();
}

zx_status_t Blob::CommitDataBuffer() {
  if (inode_.blob_size == 0) {
    // It's the null blob, so just verify.
    return Verify();
  } else {
    return data_mapping_.vmo().op_range(ZX_VMO_OP_COMMIT, 0, inode_.blob_size, nullptr, 0);
  }
}

zx_status_t Blob::LoadAndVerifyBlob(Blobfs* bs, uint32_t node_index) {
  fbl::RefPtr<Blob> vn = fbl::MakeRefCounted<Blob>(bs, node_index, *bs->GetNode(node_index));

  auto status = vn->LoadVmosFromDisk();
  if (status != ZX_OK) {
    return status;
  }

  // Blobs that are not pager-backed are already verified when they are loaded.
  // For pager-backed blobs, commit the entire blob in memory. This will cause all of the pages to
  // be verified as they are read in. Note that a separate call to Verify() is not required. If the
  // commit operation fails due to a verification failure, we do propagate the error back via the
  // return status.
  if (vn->IsPagerBacked()) {
    return vn->CommitDataBuffer();
  }
  return ZX_OK;
}

BlobCache& Blob::Cache() { return blobfs_->Cache(); }

bool Blob::ShouldCache() const {
  switch (state()) {
    // All "Valid", cacheable states, where the blob still exists on storage.
    case BlobState::kReadable:
      return true;
    default:
      return false;
  }
}

void Blob::ActivateLowMemory() {
  // We shouldn't be putting the blob into a low-memory state while it is still mapped.
  ZX_ASSERT(clone_watcher_.object() == ZX_HANDLE_INVALID);
  page_watcher_.reset();
  data_mapping_.Reset();
  merkle_mapping_.Reset();
}

Blob::~Blob() { ActivateLowMemory(); }

fs::VnodeProtocolSet Blob::GetProtocols() const { return fs::VnodeProtocol::kFile; }

bool Blob::ValidateRights(fs::Rights rights) {
  // To acquire write access to a blob, it must be empty.
  return !rights.write || state() == BlobState::kEmpty;
}

zx_status_t Blob::GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                         [[maybe_unused]] fs::Rights rights,
                                         fs::VnodeRepresentation* info) {
  zx::event observer;
  zx_status_t status = GetReadableEvent(&observer);
  if (status != ZX_OK) {
    return status;
  }
  *info = fs::VnodeRepresentation::File{.observer = std::move(observer)};
  return ZX_OK;
}

zx_status_t Blob::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  TRACE_DURATION("blobfs", "Blob::Read", "len", len, "off", off);
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kRead);

  return ReadInternal(data, len, off, out_actual);
}

zx_status_t Blob::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
  TRACE_DURATION("blobfs", "Blob::Write", "len", len, "off", offset);
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kWrite);
  return WriteInternal(data, len, out_actual);
}

zx_status_t Blob::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kAppend);
  zx_status_t status = WriteInternal(data, len, out_actual);
  if (state() == BlobState::kDataWrite) {
    ZX_DEBUG_ASSERT(write_info_ != nullptr);
    *out_actual = write_info_->bytes_written;
  } else {
    *out_actual = inode_.blob_size;
  }
  return status;
}

zx_status_t Blob::GetAttributes(fs::VnodeAttributes* a) {
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kGetAttr);
  *a = fs::VnodeAttributes();
  a->mode = V_TYPE_FILE | V_IRUSR;
  a->inode = Ino();
  a->content_size = SizeData();
  a->storage_size = inode_.block_count * kBlobfsBlockSize;
  a->link_count = 1;
  a->creation_time = 0;
  a->modification_time = 0;
  return ZX_OK;
}

zx_status_t Blob::Truncate(size_t len) {
  TRACE_DURATION("blobfs", "Blob::Truncate", "len", len);
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kTruncate);
  return PrepareWrite(len, blobfs_->ShouldCompress() && len > kCompressionSizeThresholdBytes);
}

#ifdef __Fuchsia__

zx_status_t Blob::QueryFilesystem(::llcpp::fuchsia::io::FilesystemInfo* info) {
  blobfs_->GetFilesystemInfo(info);
  return ZX_OK;
}

zx_status_t Blob::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
  return blobfs_->Device()->GetDevicePath(buffer_len, out_name, out_len);
}

zx_status_t Blob::GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) {
  TRACE_DURATION("blobfs", "Blob::GetVmo", "flags", flags);

  if (flags & ::llcpp::fuchsia::io::VMO_FLAG_WRITE) {
    return ZX_ERR_NOT_SUPPORTED;
  } else if (flags & ::llcpp::fuchsia::io::VMO_FLAG_EXACT) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Let clients map and set the names of their VMOs.
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHTS_PROPERTY;
  // We can ignore fuchsia_io_VMO_FLAG_PRIVATE, since private / shared access
  // to the underlying VMO can both be satisfied with a clone due to
  // the immutability of blobfs blobs.
  rights |= (flags & ::llcpp::fuchsia::io::VMO_FLAG_READ) ? ZX_RIGHT_READ : 0;
  rights |= (flags & ::llcpp::fuchsia::io::VMO_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
  return CloneDataVmo(rights, out_vmo, out_size);
}

#endif

void Blob::Sync(SyncCallback on_complete) {
  // This function will issue its callbacks on either the current thread or the journal thread. The
  // vnode interface says this is OK.
  TRACE_DURATION("blobfs", "Blob::Sync");
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kSync);

  SyncingState state;
  {
    std::scoped_lock guard(mutex_);
    state = syncing_state_;
  }

  switch (state) {
    case SyncingState::kDataIncomplete: {
      // It doesn't make sense to sync a partial blob since it can't have its proper
      // content-addressed name without all the data.
      on_complete(ZX_ERR_BAD_STATE);
      break;
    }
    case SyncingState::kSyncing: {
      // The blob data is complete. When this happens the Blob object will automatically write its
      // metadata, but it may not get flushed for some time. This call both encourages the sync to
      // happen "soon" and provides a way to get notified when it does.
      auto trace_id = TRACE_NONCE();
      TRACE_FLOW_BEGIN("blobfs", "Blob.sync", trace_id);
      blobfs_->Sync([evt = std::move(event),
                     on_complete = std::move(on_complete)](zx_status_t status) mutable {
        // Note: this may be executed on an arbitrary thread.
        on_complete(status);
      });
      break;
    }
    case SyncingState::kDone: {
      // All metadata has already been synced. Calling Sync() is a no-op.
      on_complete(ZX_OK);
      break;
    }
  }
}

void Blob::CompleteSync() {
  // Called on the journal thread when the syncing is complete.
  {
    std::scoped_lock guard(mutex_);
    syncing_state_ = SyncingState::kDone;
  }
}

fbl::RefPtr<Blob> Blob::CloneWatcherTeardown() {
  if (clone_watcher_.is_pending()) {
    clone_watcher_.Cancel();
    clone_watcher_.set_object(ZX_HANDLE_INVALID);
    tearing_down_ = true;
    return std::move(clone_ref_);
  }
  return nullptr;
}

zx_status_t Blob::Open([[maybe_unused]] ValidatedOptions options,
                       fbl::RefPtr<Vnode>* out_redirect) {
  fd_count_++;
  return ZX_OK;
}

zx_status_t Blob::Close() {
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kClose);
  ZX_DEBUG_ASSERT_MSG(fd_count_ > 0, "Closing blob with no fds open");
  fd_count_--;
  // Attempt purge in case blob was unlinked prior to close
  return TryPurge();
}

zx_status_t Blob::TryPurge() {
  if (Purgeable()) {
    return Purge();
  }
  return ZX_OK;
}

zx_status_t Blob::Purge() {
  ZX_DEBUG_ASSERT(fd_count_ == 0);
  ZX_DEBUG_ASSERT(Purgeable());

  if (state() == BlobState::kReadable) {
    // A readable blob should only be purged if it has been unlinked.
    ZX_ASSERT(DeletionQueued());
    BlobTransaction transaction;
    blobfs_->FreeInode(GetMapIndex(), transaction);
    transaction.Commit(*blobfs_->journal());
  }
  ZX_ASSERT(Cache().Evict(fbl::RefPtr(this)) == ZX_OK);
  set_state(BlobState::kPurged);
  return ZX_OK;
}

uint32_t Blob::GetBlockSize() const { return blobfs_->Info().block_size; }

}  // namespace blobfs
