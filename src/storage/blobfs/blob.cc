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
#include <string_view>
#include <utility>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_buffer.h>
#include <safemath/checked_math.h>

#include "src/lib/digest/digest.h"
#include "src/lib/digest/merkle-tree.h"
#include "src/lib/digest/node-digest.h"
#include "src/lib/storage/vfs/cpp/journal/data_streamer.h"
#include "src/lib/storage/vfs/cpp/metrics/events.h"
#include "src/lib/storage/vfs/cpp/transaction/writeback.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/storage/blobfs/blob_data_producer.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/blob_verifier.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression_settings.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/iterator/allocated_extent_iterator.h"
#include "src/storage/blobfs/iterator/block_iterator.h"
#include "src/storage/blobfs/iterator/extent_iterator.h"
#include "src/storage/blobfs/iterator/node_populator.h"
#include "src/storage/blobfs/iterator/vector_extent_iterator.h"
#include "src/storage/blobfs/metrics.h"

namespace blobfs {

using ::digest::Digest;
using ::digest::MerkleTreeCreator;

// Data used exclusively during writeback.
struct Blob::WriteInfo {
  // See comment for merkle_tree() below.
  static constexpr size_t kPreMerkleTreePadding = kBlobfsBlockSize;

  WriteInfo() = default;

  // Not copyable or movable because merkle_tree_creator has a pointer to digest.
  WriteInfo(const WriteInfo&) = delete;
  WriteInfo& operator=(const WriteInfo&) = delete;

  // We leave room in the merkle tree buffer to add padding before the merkle tree which might be
  // required with the compact blob layout.
  uint8_t* merkle_tree() const {
    ZX_ASSERT(merkle_tree_buffer);
    return merkle_tree_buffer.get() + kPreMerkleTreePadding;
  }

  uint64_t bytes_written = 0;

  fbl::Vector<ReservedExtent> extents;
  fbl::Vector<ReservedNode> node_indices;

  std::optional<BlobCompressor> compressor;

  // Target compressed size for this blob indicates the possible on-disk compressed size in bytes.
  std::optional<uint64_t> target_compression_size_;

  // The fused write error.  Once writing has failed, we return the same error on subsequent
  // writes in case a higher layer dropped the error and returned a short write instead.
  zx_status_t write_error = ZX_OK;

  // As data is written, we build the merkle tree using this.
  digest::MerkleTreeCreator merkle_tree_creator;

  // The merkle tree creator stores the root digest here.
  uint8_t digest[digest::kSha256Length];

  // The merkle tree creator stores the rest of the tree here.  The buffer includes space for
  // padding.  See the comment for merkle_tree() above.
  std::unique_ptr<uint8_t[]> merkle_tree_buffer;

  // The old blob that this write is replacing.
  fbl::RefPtr<Blob> old_blob;

  // Sets the target_compression_size_ field.
  void SetTargetCompressionSize(uint64_t size) {
    target_compression_size_ = std::make_optional(size);
  }
};

bool SupportsPaging(const Inode& inode) {
  zx::status<CompressionAlgorithm> status = AlgorithmForInode(inode);
  if (status.is_ok() && (status.value() == CompressionAlgorithm::UNCOMPRESSED ||
                         status.value() == CompressionAlgorithm::CHUNKED)) {
    return true;
  }
  return false;
}

zx_status_t Blob::VerifyNullBlob() const {
  ZX_ASSERT(inode_.blob_size == 0);
  std::unique_ptr<BlobVerifier> verifier;
  if (zx_status_t status =
          BlobVerifier::CreateWithoutTree(digest(), blobfs_->Metrics(), inode_.blob_size,
                                          &blobfs_->blob_corruption_notifier(), &verifier);
      status != ZX_OK) {
    return status;
  }
  return verifier->Verify(nullptr, 0, 0);
}

uint64_t Blob::SizeData() const {
  std::lock_guard lock(mutex_);
  if (state() == BlobState::kReadable) {
    return inode_.blob_size;
  }
  return 0;
}

Blob::Blob(Blobfs* bs, const Digest& digest)
    : CacheNode(bs->vfs(), digest), blobfs_(bs), clone_watcher_(this) {
  write_info_ = std::make_unique<WriteInfo>();
}

Blob::Blob(Blobfs* bs, uint32_t node_index, const Inode& inode)
    : CacheNode(bs->vfs(), Digest(inode.merkle_root_hash)),
      blobfs_(bs),
      state_(BlobState::kReadable),
      syncing_state_(SyncingState::kDone),
      map_index_(node_index),
      clone_watcher_(this),
      inode_(inode) {
  write_info_ = std::make_unique<WriteInfo>();
}

zx_status_t Blob::WriteNullBlob() {
  ZX_DEBUG_ASSERT(inode_.blob_size == 0);
  ZX_DEBUG_ASSERT(inode_.block_count == 0);

  if (zx_status_t status = VerifyNullBlob(); status != ZX_OK) {
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
    // Fail early if |size_data| would overflow when rounded up to block size.
    return ZX_ERR_OUT_OF_RANGE;
  }

  std::lock_guard lock(mutex_);
  if (state() != BlobState::kEmpty) {
    return ZX_ERR_BAD_STATE;
  }

  // Fail early if target_compression_size is set is not sane.
  if (write_info_->target_compression_size_.has_value() &&
      (write_info_->target_compression_size_.value() == 0 ||
       (write_info_->target_compression_size_.value()) == std::numeric_limits<uint64_t>::max())) {
    FX_LOGS(ERROR) << "Target compressed size is invalid: "
                   << write_info_->target_compression_size_.value();
    return ZX_ERR_INVALID_ARGS;
  }

  memset(inode_.merkle_root_hash, 0, sizeof(inode_.merkle_root_hash));
  inode_.blob_size = size_data;

  // Reserve a node for blob's inode. We might need more nodes for extents later.
  zx_status_t status = blobfs_->GetAllocator()->ReserveNodes(1, &write_info_->node_indices);
  if (status != ZX_OK) {
    return status;
  }
  map_index_ = write_info_->node_indices[0].index();

  // For compressed blobs, we only write into the compression buffer.  For uncompressed blobs we
  // write into the data vmo.
  if (compress) {
    write_info_->compressor =
        BlobCompressor::Create(blobfs_->write_compression_settings(), inode_.blob_size);
    if (!write_info_->compressor) {
      // TODO(fxbug.dev/70356)Make BlobCompressor::Create return the actual error instead.
      // Replace ZX_ERR_INTERNAL with the correct error once fxbug.dev/70356 is fixed.
      FX_LOGS(ERROR) << "Failed to initialize compressor: " << ZX_ERR_INTERNAL;
      return ZX_ERR_INTERNAL;
    }
  } else if (inode_.blob_size != 0) {
    if ((status = PrepareDataVmoForWriting()) != ZX_OK) {
      return status;
    }
  }

  write_info_->merkle_tree_creator.SetUseCompactFormat(
      ShouldUseCompactMerkleTreeFormat(GetBlobLayoutFormat(blobfs_->Info())));
  if ((status = write_info_->merkle_tree_creator.SetDataLength(inode_.blob_size)) != ZX_OK) {
    return status;
  }
  const size_t tree_len = write_info_->merkle_tree_creator.GetTreeLength();
  // Allow for zero padding before and after.
  write_info_->merkle_tree_buffer =
      std::make_unique<uint8_t[]>(tree_len + WriteInfo::kPreMerkleTreePadding);
  if ((status = write_info_->merkle_tree_creator.SetTree(write_info_->merkle_tree(), tree_len,
                                                         &write_info_->digest,
                                                         sizeof(write_info_->digest))) != ZX_OK) {
    return status;
  }

  set_state(BlobState::kDataWrite);

  // Special case for the null blob: We skip the write phase.
  return inode_.blob_size == 0 ? WriteNullBlob() : ZX_OK;
}

void Blob::SetOldBlob(Blob& blob) {
  std::lock_guard lock(mutex_);
  write_info_->old_blob = fbl::RefPtr(&blob);
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

bool Blob::IsDataLoaded() const { return vmo().is_valid(); }

bool Blob::IsPagerBacked() const {
  return SupportsPaging(inode_) && state() == BlobState::kReadable;
}

zx_status_t Blob::WriteMetadata(BlobTransaction& transaction) {
  TRACE_DURATION("blobfs", "Blobfs::WriteMetadata");
  assert(state() == BlobState::kDataWrite);

  // Update the on-disk hash.
  digest().CopyTo(inode_.merkle_root_hash);

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

    auto mapped_inode_or_error = blobfs_->GetNode(map_index_);
    if (mapped_inode_or_error.is_error()) {
      return mapped_inode_or_error.status_value();
    }
    InodePtr mapped_inode = std::move(mapped_inode_or_error).value();
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
    auto mapped_inode_or_error = blobfs_->GetNode(map_index_);
    if (mapped_inode_or_error.is_error()) {
      return mapped_inode_or_error.status_value();
    }
    InodePtr mapped_inode = std::move(mapped_inode_or_error).value();
    *mapped_inode = inode_;
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
    if (zx_status_t status = vmo().write(data, offset, to_write); status != ZX_OK) {
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
  if (digest() != write_info_->digest) {
    // Downloaded blob did not match provided digest.
    FX_LOGS(ERROR) << "downloaded blob did not match provided digest " << digest();
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

  std::variant<std::monostate, DecompressBlobDataProducer, SimpleBlobDataProducer> data;
  BlobDataProducer* data_ptr = nullptr;

  if (compress) {
    // The data comes from the compression buffer.
    data_ptr = &data.emplace<SimpleBlobDataProducer>(
        fbl::Span(static_cast<const uint8_t*>(write_info_->compressor->Data()),
                  write_info_->compressor->Size()));
    SetCompressionAlgorithm(&inode_, write_info_->compressor->algorithm());
  } else if (write_info_->compressor) {
    // In this case, we've decided against compressing because there are no savings, so we have to
    // decompress.
    if (auto producer_or =
            DecompressBlobDataProducer::Create(*write_info_->compressor, inode_.blob_size);
        producer_or.is_error()) {
      return producer_or.error_value();
    } else {
      data_ptr = &data.emplace<DecompressBlobDataProducer>(std::move(producer_or).value());
    }
  } else {
    // The data comes from the data buffer.
    data_ptr = &data.emplace<SimpleBlobDataProducer>(
        fbl::Span(static_cast<const uint8_t*>(data_mapping_.start()), inode_.blob_size));
  }

  SimpleBlobDataProducer merkle(fbl::Span(write_info_->merkle_tree(), merkle_size));

  MergeBlobDataProducer producer = [&]() {
    switch (blob_layout->Format()) {
      case BlobLayoutFormat::kPaddedMerkleTreeAtStart:
        // Write the merkle data first followed by the data.  The merkle data should be a multiple
        // of the block size so we don't need any padding.
        ZX_ASSERT(merkle.GetRemainingBytes() % kBlobfsBlockSize == 0);
        return MergeBlobDataProducer(merkle, *data_ptr, /*padding=*/0);
      case BlobLayoutFormat::kCompactMerkleTreeAtEnd:
        // Write the data followed by the merkle tree.  There might be some padding between the
        // data and the merkle tree.
        return MergeBlobDataProducer(
            *data_ptr, merkle, blob_layout->MerkleTreeOffset() - data_ptr->GetRemainingBytes());
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
  data_mapping_.Unmap();
  FreeVmo();
  write_info_->compressor.reset();

  // Wrap all pending writes with a strong reference to this Blob, so that it stays
  // alive while there are writes in progress acting on it.
  BlobTransaction transaction;
  if (zx_status_t status = WriteMetadata(transaction); status != ZX_OK) {
    return status;
  }
  if (write_info_->old_blob) {
    ZX_ASSERT(blobfs_->FreeInode(write_info_->old_blob->Ino(), transaction) == ZX_OK);
    auto& cache = Cache();
    ZX_ASSERT(cache.Evict(write_info_->old_blob) == ZX_OK);
    ZX_ASSERT(cache.Add(fbl::RefPtr(this)) == ZX_OK);
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

zx_status_t Blob::WriteData(uint32_t block_count, BlobDataProducer& producer,
                            fs::DataStreamer& streamer) {
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
  { syncing_state_ = SyncingState::kSyncing; }
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

  zx::vmo clone;
  status = vmo().create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, inode_.blob_size, &clone);
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
    clone_watcher_.set_object(vmo().get());
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

// TODO(fxbug.dev/51111) This is not used with the new pager. Remove this code when the transition
// is complete.
void Blob::HandleNoClones(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal) {
  fbl::RefPtr<Blob> local_clone_ref;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (IsDataLoaded()) {
      zx_info_vmo_t info;
      zx_status_t info_status = vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr);
      if (info_status == ZX_OK) {
        if (info.num_children > 0) {
          // A clone was added at some point since the asynchronous HandleNoClones call was
          // enqueued. Re-arm the watcher, and return.
          //
          // clone_watcher_ is level triggered, so even if there are no clones now (since the call
          // to get_info), HandleNoClones will still be enqueued.
          //
          // No new clones can be added during this function, since clones are added on the main
          // dispatch thread which is currently running this function. If blobfs becomes
          // multi-threaded, locking will be necessary here.
          clone_watcher_.set_object(vmo().get());
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

    // Save the clone ref to the stack to prevent releasing it (and hence ourselves) inside the
    // lock.
    local_clone_ref = std::move(clone_ref_);
  }

  // Free the clone ref (outside the lock).
  //
  // This will trigger recycling which will call back into us via an external (non-lock-held)
  // function, so must be done outside of the lock to avoid reentrant locking.
  local_clone_ref = nullptr;

  // This might have been the last reference to a deleted blob, so try purging it.
  std::lock_guard<std::mutex> lock(mutex_);
  if (zx_status_t status = TryPurge(); status != ZX_OK) {
    FX_LOGS(WARNING) << "Purging blob " << digest() << " failed: " << zx_status_get_string(status);
  }
  if (!HasReferences()) {
    fbl::StringBuffer<ZX_MAX_NAME_LEN> data_vmo_name;
    FormatInactiveBlobDataVmoName(inode_, &data_vmo_name);
    if (zx_status_t status =
            vmo().set_property(ZX_PROP_NAME, data_vmo_name.c_str(), data_vmo_name.size());
        status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to update blob VMO name: " << zx_status_get_string(status);
    }
  }
}

zx_status_t Blob::ReadInternal(void* data, size_t len, size_t off, size_t* actual) {
  TRACE_DURATION("blobfs", "Blobfs::ReadInternal", "len", len, "off", off);

  // TODO(fxbug.dev/74088) allow multiple reads at a time as long as the blob is completely
  // loaded. This may involve moving the lock to LoadVmosFromDisk and auditing the inode_ and vmo_
  // usage in the code below to see if we can guarantee it won't change out from under us outside
  // the lock.
  std::lock_guard lock(mutex_);

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

  status = vmo().read(data, off, len);
  if (status == ZX_OK) {
    *actual = len;
  }
  return status;
}

#if defined(ENABLE_BLOBFS_NEW_PAGER)

// New pager implementation.
zx_status_t Blob::LoadPagedVmosFromDisk() {
  ZX_ASSERT(!IsDataLoaded());

  // If there is an overriden cache policy for pager-backed blobs, apply it now. Otherwise the
  // system-wide default will be used.
  std::optional<CachePolicy> cache_policy = blobfs_->pager_backed_cache_policy();
  if (cache_policy) {
    set_overridden_cache_policy(*cache_policy);
  }

  zx::status<BlobLoader::PagedLoadResult> load_result =
      blobfs_->loader().LoadBlobPaged(map_index_, &blobfs_->blob_corruption_notifier());
  if (load_result.is_error())
    return load_result.error_value();

  // Make the vmo.
  if (auto status = EnsureCreateVmo(load_result->layout->FileBlockAlignedSize()); status.is_error())
    return status.error_value();

  // Commit the other load information.
  pager_info_ = std::move(load_result->pager_info);
  merkle_mapping_ = std::move(load_result->merkle);

  return ZX_OK;
}

#else

// Old pager.
zx_status_t Blob::LoadPagedVmosFromDisk() {
  ZX_ASSERT(!IsDataLoaded());

  // If there is an overriden cache policy for pager-backed blobs, apply it now. Otherwise the
  // system-wide default will be used.
  std::optional<CachePolicy> cache_policy = blobfs_->pager_backed_cache_policy();
  if (cache_policy) {
    set_overridden_cache_policy(*cache_policy);
  }

  zx::status<BlobLoader::PagedLoadResult> load_result =
      blobfs_->loader().LoadBlobPaged(map_index_, &blobfs_->blob_corruption_notifier());
  if (load_result.is_error())
    return load_result.error_value();

  auto created_page_watcher =
      std::make_unique<pager::PageWatcher>(blobfs_->pager(), std::move(load_result->pager_info));

  // Make the vmo.
  zx::vmo data_vmo;
  if (zx_status_t status = created_page_watcher->CreatePagedVmo(
          load_result->layout->FileBlockAlignedSize(), &data_vmo);
      status != ZX_OK) {
    return status;
  }

  // Commit the load artifacts now that all setup has succeeded.
  merkle_mapping_ = std::move(load_result->merkle);
  vmo_ = std::move(data_vmo);
  page_watcher_ = std::move(created_page_watcher);

  return ZX_OK;
}

#endif

zx_status_t Blob::LoadUnpagedVmosFromDisk() {
#if defined(ENABLE_BLOBFS_NEW_PAGER)
  // TODO(fxbug.dev/51111): Figure out if we need to support unpaged blobs in the new system.
  return ZX_ERR_NOT_SUPPORTED;
#else
  ZX_ASSERT(!IsDataLoaded());

  zx::status<BlobLoader::UnpagedLoadResult> load_result =
      blobfs_->loader().LoadBlob(map_index_, &blobfs_->blob_corruption_notifier());
  if (load_result.is_ok()) {
    data_mapping_ = std::move(load_result->data_mapper);
    merkle_mapping_ = std::move(load_result->merkle);
    vmo_ = std::move(load_result->data_vmo);
  }

  return load_result.status_value();
#endif
}

zx_status_t Blob::LoadVmosFromDisk() {
  if (IsDataLoaded())
    return ZX_OK;

  if (inode_.blob_size == 0) {
    // Null blobs don't need any loading, just verification that they're correct.
    return VerifyNullBlob();
  }

  zx_status_t status;
  if (IsPagerBacked()) {
    status = LoadPagedVmosFromDisk();
  } else {
    status = LoadUnpagedVmosFromDisk();
  }

  if (status == ZX_OK)
    SetVmoName();

  syncing_state_ = SyncingState::kDone;
  return status;
}

zx_status_t Blob::PrepareDataVmoForWriting() {
  if (IsDataLoaded())
    return ZX_OK;

#if defined(ENABLE_BLOBFS_NEW_PAGER)
  // TODO(fxbug.dev/51111): Blob writing needs to be addressed in the new pager. The problem is
  // that we can not write directly into the paged VMO (this will cause the kernel to try to fault
  // it in so we can write to it, which will call back into us).
  //
  // The write path either needs to have a different mode where the vmo() isn't registered with the
  // pager, or we need to write into a parallel write VMO. The latter option would be better
  // because it would enable us to page from just-written blobs (currently you have to close and
  // reopen the blob to support paging).
  return ZX_ERR_NOT_SUPPORTED;
#else

  uint64_t block_aligned_size = fbl::round_up(inode_.blob_size, kBlobfsBlockSize);
  if (zx_status_t status = data_mapping_.CreateAndMap(
          block_aligned_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo_);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to map data vmo: " << zx_status_get_string(status);
    return status;
  }

  SetVmoName();

  return ZX_OK;
#endif
}

zx_status_t Blob::QueueUnlink() {
  std::lock_guard lock(mutex_);

  deletable_ = true;
  // Attempt to purge in case the blob has been unlinked with no open fds
  return TryPurge();
}

zx_status_t Blob::CommitDataBuffer() {
  if (inode_.blob_size == 0) {
    // It's the null blob, so just verify.
    return VerifyNullBlob();
  }
  return vmo().op_range(ZX_VMO_OP_COMMIT, 0, inode_.blob_size, nullptr, 0);
}

zx_status_t Blob::Verify() {
  std::lock_guard lock(mutex_);
  if (auto status = LoadVmosFromDisk(); status != ZX_OK) {
    return status;
  }

  // Blobs that are not pager-backed are already verified when they are loaded.
  // For pager-backed blobs, commit the entire blob in memory. This will cause all of the pages to
  // be verified as they are read in (or for the null bob we just verify immediately).
  // If the commit operation fails due to a verification failure, we do propagate the error back via
  // the return status.
  if (IsPagerBacked()) {
    return CommitDataBuffer();
  }
  return ZX_OK;
}

#if defined(ENABLE_BLOBFS_NEW_PAGER)
void Blob::OnNoClones() {
  // Do nothing. The default implementation will free the VMO but we always need to keep it
  // around and registered with the pager because FIDL reads go through VMO read code path.
}
#endif

BlobCache& Blob::Cache() { return blobfs_->Cache(); }

bool Blob::ShouldCache() const {
  std::lock_guard lock(mutex_);
  switch (state()) {
    // All "Valid", cacheable states, where the blob still exists on storage.
    case BlobState::kReadable:
      return true;
    default:
      return false;
  }
}

void Blob::ActivateLowMemory() {
  std::lock_guard<std::mutex> lock(mutex_);

  // We shouldn't be putting the blob into a low-memory state while it is still mapped.
  ZX_ASSERT(!has_clones());

#if !defined(ENABLE_BLOBFS_NEW_PAGER)
  // This must happen before freeing the vmo for the PageWatcher's pager synchronization code in
  // its destructor to work.
  page_watcher_.reset();
#endif

  FreeVmo();

  data_mapping_.Unmap();
  merkle_mapping_.Reset();
}

Blob::~Blob() { ActivateLowMemory(); }

fs::VnodeProtocolSet Blob::GetProtocols() const { return fs::VnodeProtocol::kFile; }

bool Blob::ValidateRights(fs::Rights rights) {
  // To acquire write access to a blob, it must be empty.
  //
  // TODO(fxbug.dev/67659) If we run FIDL on multiple threads (we currently don't) there is a race
  // condition here where another thread could start writing at the same time. Decide whether we
  // support FIDL from multiple threads and if so, whether this condition is important.
  std::lock_guard lock(mutex_);
  return !rights.write || state() == BlobState::kEmpty;
}

zx_status_t Blob::GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                         [[maybe_unused]] fs::Rights rights,
                                         fs::VnodeRepresentation* info) {
  std::lock_guard lock(mutex_);

  zx::event observer;
  if (zx_status_t status = GetReadableEvent(&observer); status != ZX_OK) {
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

  std::lock_guard lock(mutex_);
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kWrite);
  return WriteInternal(data, len, out_actual);
}

zx_status_t Blob::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kAppend);

  std::lock_guard lock(mutex_);

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

  // SizeData() expects to be called outside the lock.
  auto content_size = SizeData();

  std::lock_guard lock(mutex_);

  *a = fs::VnodeAttributes();
  a->mode = V_TYPE_FILE | V_IRUSR;
  a->inode = map_index_;
  a->content_size = content_size;
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

void Blob::SetTargetCompressionSize(uint64_t size) {
  std::lock_guard lock(mutex_);
  write_info_.get()->SetTargetCompressionSize(size);
}

#ifdef __Fuchsia__

zx_status_t Blob::QueryFilesystem(fuchsia_io::wire::FilesystemInfo* info) {
  blobfs_->GetFilesystemInfo(info);
  return ZX_OK;
}

zx_status_t Blob::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
  return blobfs_->Device()->GetDevicePath(buffer_len, out_name, out_len);
}

zx_status_t Blob::GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) {
  TRACE_DURATION("blobfs", "Blob::GetVmo", "flags", flags);

  std::lock_guard lock(mutex_);

  if (flags & fuchsia_io::wire::VMO_FLAG_WRITE) {
    return ZX_ERR_NOT_SUPPORTED;
  } else if (flags & fuchsia_io::wire::VMO_FLAG_EXACT) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Let clients map and set the names of their VMOs.
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHTS_PROPERTY;
  // We can ignore fuchsia_io_VMO_FLAG_PRIVATE, since private / shared access
  // to the underlying VMO can both be satisfied with a clone due to
  // the immutability of blobfs blobs.
  rights |= (flags & fuchsia_io::wire::VMO_FLAG_READ) ? ZX_RIGHT_READ : 0;
  rights |= (flags & fuchsia_io::wire::VMO_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
  return CloneDataVmo(rights, out_vmo, out_size);
}

#endif  // defined(__Fuchsia__)

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

#if defined(ENABLE_BLOBFS_NEW_PAGER)
// This function will get called on an arbitrary pager worker thread.
void Blob::VmoRead(uint64_t offset, uint64_t length) {
  TRACE_DURATION("blobfs", "Blob::VmoRead", "offset", offset, "length", length);

  std::lock_guard<std::mutex> lock(mutex_);

  ZX_DEBUG_ASSERT(IsDataLoaded());

  if (is_corrupt_) {
    FX_LOGS(ERROR) << "Blobfs failing page request because blob was previously found corrupt.";
    if (auto error_result = paged_vfs()->ReportPagerError(vmo(), offset, length, ZX_ERR_BAD_STATE);
        error_result.is_error()) {
      FX_LOGS(ERROR) << "Failed to report pager error to kernel: " << error_result.status_string();
    }
    return;
  }

  // TODO(fxbug.dev/51111) This gets the pager state out of the PageWatcher to avoid forking the
  // code too much during the pager code transition. When the old pager is not needed, the
  // PageWatcher should be deleted and its state moved into this class.
  pager::PagerErrorStatus pager_error_status =
      blobfs_->pager()->TransferPagesToVmo(offset, length, vmo(), pager_info_);
  if (pager_error_status != pager::PagerErrorStatus::kOK) {
    FX_LOGS(ERROR) << "Pager failed to transfer pages to the blob, error: "
                   << zx_status_get_string(static_cast<zx_status_t>(pager_error_status));
    if (auto error_result = paged_vfs()->ReportPagerError(
            vmo(), offset, length, static_cast<zx_status_t>(pager_error_status));
        error_result.is_error()) {
      FX_LOGS(ERROR) << "Failed to report pager error to kernel: " << error_result.status_string();
    }

    // We've signaled a failure and unblocked outstanding page requests for this range. If the pager
    // error was a verification error, fail future requests as well - we should not service further
    // page requests on a corrupt blob.
    //
    // Note that we cannot simply detach the VMO from the pager here. There might be outstanding
    // page requests which have been queued but are yet to be serviced. These need to be handled
    // correctly - if the VMO is detached, there will be no way for us to communicate failure to
    // the kernel, since zx_pager_op_range() requires a valid pager VMO handle. Without being able
    // to make a call to zx_pager_op_range() to indicate a failed page request, the faulting thread
    // would hang indefinitely.
    if (pager_error_status == pager::PagerErrorStatus::kErrDataIntegrity)
      is_corrupt_ = true;
  }
}
#endif

bool Blob::HasReferences() const { return open_count() > 0 || has_clones(); }

void Blob::CompleteSync() {
  // Called on the journal thread when the syncing is complete.
  {
    std::scoped_lock guard(mutex_);
    syncing_state_ = SyncingState::kDone;
  }
}

// TODO(fxbug.dev/51111) This is not used with the new pager. Remove this code when the transition
// is complete.
fbl::RefPtr<Blob> Blob::CloneWatcherTeardown() {
  std::lock_guard lock(mutex_);
  if (clone_watcher_.is_pending()) {
    clone_watcher_.Cancel();
    clone_watcher_.set_object(ZX_HANDLE_INVALID);
    tearing_down_ = true;
    return std::move(clone_ref_);
  }
  return nullptr;
}

zx_status_t Blob::OpenNode([[maybe_unused]] ValidatedOptions options,
                           fbl::RefPtr<Vnode>* out_redirect) {
  std::lock_guard lock(mutex_);
  if (IsDataLoaded()) {
    SetVmoName();
  }
  return ZX_OK;
}

zx_status_t Blob::CloseNode() {
  std::lock_guard lock(mutex_);

  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kClose);
  if (IsDataLoaded() && !HasReferences()) {
    fbl::StringBuffer<ZX_MAX_NAME_LEN> data_vmo_name;
    FormatInactiveBlobDataVmoName(inode_, &data_vmo_name);
    if (zx_status_t status =
            vmo().set_property(ZX_PROP_NAME, data_vmo_name.data(), data_vmo_name.size());
        status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to update blob VMO name: " << zx_status_get_string(status);
    }
  }
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
  ZX_DEBUG_ASSERT(Purgeable());

  if (state() == BlobState::kReadable) {
    // A readable blob should only be purged if it has been unlinked.
    ZX_ASSERT(deletable_);
    BlobTransaction transaction;
    ZX_ASSERT(blobfs_->FreeInode(map_index_, transaction) == ZX_OK);
    transaction.Commit(*blobfs_->journal());
  }
  ZX_ASSERT(Cache().Evict(fbl::RefPtr(this)) == ZX_OK);
  set_state(BlobState::kPurged);
  return ZX_OK;
}

uint32_t Blob::GetBlockSize() const { return blobfs_->Info().block_size; }

void Blob::SetVmoName() {
  fbl::StringBuffer<ZX_MAX_NAME_LEN> name;
  FormatBlobDataVmoName(inode_, &name);
  vmo().set_property(ZX_PROP_NAME, name.data(), name.size());
}

}  // namespace blobfs
