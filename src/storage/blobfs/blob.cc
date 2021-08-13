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
#include <iterator>
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
    ZX_ASSERT_MSG(merkle_tree_buffer, "Merkle tree buffer should not be nullptr");
    return merkle_tree_buffer.get() + kPreMerkleTreePadding;
  }

  uint64_t bytes_written = 0;

  std::vector<ReservedExtent> extents;
  std::vector<ReservedNode> node_indices;

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

zx_status_t Blob::VerifyNullBlob() const {
  ZX_ASSERT_MSG(blob_size_ == 0, "Inode blob size is not zero :%lu", blob_size_);
  auto verifier_or = BlobVerifier::CreateWithoutTree(digest(), blobfs_->GetMetrics(), 0,
                                                     &blobfs_->blob_corruption_notifier());
  if (verifier_or.is_error())
    return verifier_or.error_value();
  return verifier_or->Verify(nullptr, 0, 0);
}

uint64_t Blob::SizeData() const {
  std::lock_guard lock(mutex_);
  if (state() == BlobState::kReadable)
    return blob_size_;
  return 0;
}

Blob::Blob(Blobfs* bs, const digest::Digest& digest) : CacheNode(bs->vfs(), digest), blobfs_(bs) {
  write_info_ = std::make_unique<WriteInfo>();
}

Blob::Blob(Blobfs* bs, uint32_t node_index, const Inode& inode)
    : CacheNode(bs->vfs(), digest::Digest(inode.merkle_root_hash)),
      blobfs_(bs),
      state_(BlobState::kReadable),
      syncing_state_(SyncingState::kDone),
      map_index_(node_index),
      blob_size_(inode.blob_size),
      block_count_(inode.block_count) {
  write_info_ = std::make_unique<WriteInfo>();
}

zx_status_t Blob::WriteNullBlob() {
  ZX_DEBUG_ASSERT(blob_size_ == 0);
  ZX_DEBUG_ASSERT(block_count_ == 0);

  if (zx_status_t status = VerifyNullBlob(); status != ZX_OK) {
    return status;
  }

  BlobTransaction transaction;
  if (zx_status_t status = WriteMetadata(transaction, CompressionAlgorithm::kUncompressed);
      status != ZX_OK) {
    return status;
  }
  transaction.Commit(*blobfs_->GetJournal(), {},
                     [blob = fbl::RefPtr(this)]() { blob->CompleteSync(); });

  return MarkReadable(CompressionAlgorithm::kUncompressed);
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

  blob_size_ = size_data;

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
        BlobCompressor::Create(blobfs_->write_compression_settings(), blob_size_);
    if (!write_info_->compressor) {
      // TODO(fxbug.dev/70356)Make BlobCompressor::Create return the actual error instead.
      // Replace ZX_ERR_INTERNAL with the correct error once fxbug.dev/70356 is fixed.
      FX_LOGS(ERROR) << "Failed to initialize compressor: " << ZX_ERR_INTERNAL;
      return ZX_ERR_INTERNAL;
    }
  } else if (blob_size_ != 0) {
    if ((status = PrepareDataVmoForWriting()) != ZX_OK) {
      return status;
    }
  }

  write_info_->merkle_tree_creator.SetUseCompactFormat(
      ShouldUseCompactMerkleTreeFormat(GetBlobLayoutFormat(blobfs_->Info())));
  if ((status = write_info_->merkle_tree_creator.SetDataLength(blob_size_)) != ZX_OK) {
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
  return blob_size_ == 0 ? WriteNullBlob() : ZX_OK;
}

void Blob::SetOldBlob(Blob& blob) {
  std::lock_guard lock(mutex_);
  write_info_->old_blob = fbl::RefPtr(&blob);
}

zx_status_t Blob::SpaceAllocate(uint32_t block_count) {
  TRACE_DURATION("blobfs", "Blobfs::SpaceAllocate", "block_count", block_count);
  ZX_ASSERT_MSG(block_count != 0, "Block count should not be zero.");

  fs::Ticker ticker(blobfs_->GetMetrics()->Collecting());

  std::vector<ReservedExtent> extents;
  std::vector<ReservedNode> nodes;

  // Reserve space for the blob.
  const uint64_t reserved_blocks = blobfs_->GetAllocator()->ReservedBlockCount();
  zx_status_t status = blobfs_->GetAllocator()->ReserveBlocks(block_count, &extents);
  if (status == ZX_ERR_NO_SPACE && reserved_blocks > 0) {
    // It's possible that a blob has just been unlinked but has yet to be flushed through the
    // journal, and the blocks are still reserved, so if that looks likely, force a flush and then
    // try again.  This might need to be revisited if/when blobfs becomes multi-threaded.
    sync_completion_t sync;
    blobfs_->Sync([&](zx_status_t) { sync_completion_signal(&sync); });
    sync_completion_wait(&sync, ZX_TIME_INFINITE);
    status = blobfs_->GetAllocator()->ReserveBlocks(block_count, &extents);
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

  // Reserve space for all additional nodes necessary to contain this blob. The inode has already
  // been reserved in Blob::PrepareWrite. Hence, we need to reserve one less node here.
  size_t node_count = NodePopulator::NodeCountForExtents(extent_count) - 1;
  status = blobfs_->GetAllocator()->ReserveNodes(node_count, &nodes);
  if (status != ZX_OK) {
    return status;
  }

  write_info_->extents = std::move(extents);
  write_info_->node_indices.insert(write_info_->node_indices.end(),
                                   std::make_move_iterator(nodes.begin()),
                                   std::make_move_iterator(nodes.end()));
  block_count_ = block_count;
  blobfs_->GetMetrics()->UpdateAllocation(blob_size_, ticker.End());
  return ZX_OK;
}

bool Blob::IsDataLoaded() const {
  // Data is served out of the paged_vmo() when paged and the unpaged_backing_data_ when not, so
  // either indicates validity.
  return paged_vmo().is_valid() || unpaged_backing_data_.is_valid();
}

zx_status_t Blob::WriteMetadata(BlobTransaction& transaction,
                                CompressionAlgorithm compression_algorithm) {
  TRACE_DURATION("blobfs", "Blobfs::WriteMetadata");
  assert(state() == BlobState::kDataWrite);

  if (block_count_) {
    // We utilize the NodePopulator class to take our reserved blocks and nodes and fill the
    // persistent map with an allocated inode / container.

    // If |on_node| is invoked on a node, it means that node was necessary to represent this
    // blob. Persist the node back to durable storage.
    auto on_node = [this, &transaction](uint32_t node_index) {
      blobfs_->PersistNode(node_index, transaction);
    };

    // If |on_extent| is invoked on an extent, it was necessary to represent this blob. Persist
    // the allocation of these blocks back to durable storage.
    auto on_extent = [this, &transaction](ReservedExtent& extent) {
      blobfs_->PersistBlocks(extent, transaction);
      return NodePopulator::IterationCommand::Continue;
    };

    auto mapped_inode_or_error = blobfs_->GetNode(map_index_);
    if (mapped_inode_or_error.is_error()) {
      return mapped_inode_or_error.status_value();
    }
    InodePtr mapped_inode = std::move(mapped_inode_or_error).value();
    *mapped_inode = Inode{
        .blob_size = blob_size_,
        .block_count = block_count_,
    };
    digest().CopyTo(mapped_inode->merkle_root_hash);
    NodePopulator populator(blobfs_->GetAllocator(), std::move(write_info_->extents),
                            std::move(write_info_->node_indices));
    zx_status_t status = populator.Walk(on_node, on_extent);
    ZX_ASSERT_MSG(status == ZX_OK, "populator.Walk failed with error: %s",
                  zx_status_get_string(status));
    SetCompressionAlgorithm(&*mapped_inode, compression_algorithm);
  } else {
    // Special case: Empty node.
    ZX_DEBUG_ASSERT(write_info_->node_indices.size() == 1);
    auto mapped_inode_or_error = blobfs_->GetNode(map_index_);
    if (mapped_inode_or_error.is_error()) {
      return mapped_inode_or_error.status_value();
    }
    InodePtr mapped_inode = std::move(mapped_inode_or_error).value();
    *mapped_inode = Inode{};
    digest().CopyTo(mapped_inode->merkle_root_hash);
    blobfs_->GetAllocator()->MarkInodeAllocated(std::move(write_info_->node_indices[0]));
    blobfs_->PersistNode(map_index_, transaction);
  }
  return ZX_OK;
}

zx_status_t Blob::WriteInternal(const void* data, size_t len,
                                std::optional<size_t> requested_offset, size_t* actual) {
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

  const size_t to_write = std::min(len, blob_size_ - write_info_->bytes_written);
  const size_t offset = write_info_->bytes_written;
  if (requested_offset && *requested_offset != offset) {
    FX_LOGS(ERROR) << "only append is currently supported (requested_offset: " << *requested_offset
                   << ", expected: " << offset << ")";
    return ZX_ERR_NOT_SUPPORTED;
  }

  *actual = to_write;
  write_info_->bytes_written += to_write;

  if (write_info_->compressor) {
    if (zx_status_t status = write_info_->compressor->Update(data, to_write); status != ZX_OK) {
      return status;
    }
  } else {
    // In the uncompressed case, the backing vmo should have been set up to write into already.
    ZX_ASSERT(unpaged_backing_data_.is_valid());
    if (zx_status_t status = unpaged_backing_data_.write(data, offset, to_write); status != ZX_OK) {
      FX_LOGS(ERROR) << "VMO write failed: " << zx_status_get_string(status);
      return status;
    }
  }

  if (zx_status_t status = write_info_->merkle_tree_creator.Append(data, to_write);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "MerkleTreeCreator::Append failed: " << zx_status_get_string(status);
    return status;
  }

  // More data to write.
  if (write_info_->bytes_written < blob_size_) {
    return ZX_OK;
  }

  if (zx_status_t status = Commit(); status != ZX_OK) {
    // Record the status so that if called again, we return the same status again.  This is done
    // because it's possible that the end-user managed to partially write some data to this blob in
    // which case the error could be dropped (by zxio or some other layer) and a short write
    // returned instead.  If this happens, the end-user will retry at which point it's helpful if we
    // return the same error rather than ZX_ERR_BAD_STATE (see above).
    write_info_->write_error = status;
    MarkError();
    return status;
  }

  return ZX_OK;
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
    if (write_info_->compressor->algorithm() == CompressionAlgorithm::kChunked &&
        fbl::round_up(write_info_->compressor->Size() + merkle_size, kBlobfsBlockSize) >=
            fbl::round_up(blob_size_ + merkle_size, kBlobfsBlockSize)) {
      compress = false;
    }
  }

  fs::Duration generation_time;

  const uint64_t data_size = compress ? write_info_->compressor->Size() : blob_size_;
  auto blob_layout = BlobLayout::CreateFromSizes(GetBlobLayoutFormat(blobfs_->Info()), blob_size_,
                                                 data_size, GetBlockSize());
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
  fzl::VmoMapper data_mapping;
  CompressionAlgorithm compression_algorithm = CompressionAlgorithm::kUncompressed;

  if (compress) {
    // The data comes from the compression buffer.
    data_ptr = &data.emplace<SimpleBlobDataProducer>(
        fbl::Span(static_cast<const uint8_t*>(write_info_->compressor->Data()),
                  write_info_->compressor->Size()));
    compression_algorithm = write_info_->compressor->algorithm();
  } else if (write_info_->compressor) {
    // In this case, we've decided against compressing because there are no savings, so we have to
    // decompress.
    if (auto producer_or = DecompressBlobDataProducer::Create(*write_info_->compressor, blob_size_);
        producer_or.is_error()) {
      return producer_or.error_value();
    } else {
      data_ptr = &data.emplace<DecompressBlobDataProducer>(std::move(producer_or).value());
    }
  } else {
    // The data comes from the data buffer.
    ZX_ASSERT(unpaged_backing_data_.is_valid());
    uint64_t block_aligned_size = fbl::round_up(blob_size_, kBlobfsBlockSize);
    if (zx_status_t status = data_mapping.Map(unpaged_backing_data_, 0, block_aligned_size);
        status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to map blob VMO: " << zx_status_get_string(status);
      return status;
    }
    data_ptr = &data.emplace<SimpleBlobDataProducer>(
        fbl::Span(static_cast<const uint8_t*>(data_mapping.start()), blob_size_));
  }

  SimpleBlobDataProducer merkle(fbl::Span(write_info_->merkle_tree(), merkle_size));

  MergeBlobDataProducer producer = [&]() {
    switch (blob_layout->Format()) {
      case BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart:
        // Write the merkle data first followed by the data.  The merkle data should be a multiple
        // of the block size so we don't need any padding.
        ZX_ASSERT_MSG(merkle.GetRemainingBytes() % kBlobfsBlockSize == 0,
                      "Merkle data size :%lu not a multiple of blobfs block size %u",
                      merkle.GetRemainingBytes(), kBlobfsBlockSize);
        return MergeBlobDataProducer(merkle, *data_ptr, /*padding=*/0);
      case BlobLayoutFormat::kCompactMerkleTreeAtEnd:
        // Write the data followed by the merkle tree.  There might be some padding between the
        // data and the merkle tree.
        return MergeBlobDataProducer(
            *data_ptr, merkle, blob_layout->MerkleTreeOffset() - data_ptr->GetRemainingBytes());
    }
  }();

  fs::DataStreamer streamer(blobfs_->GetJournal(), blobfs_->WriteBufferBlockCount());
  if (zx_status_t status = WriteData(total_block_count, producer, streamer); status != ZX_OK) {
    return status;
  }

  // No more data to write. Flush to disk.
  fs::Ticker ticker(blobfs_->GetMetrics()->Collecting());  // Tracking enqueue time.

  // Enqueue the blob's final data work. Metadata must be enqueued separately.
  zx_status_t data_status = ZX_ERR_IO;
  sync_completion_t data_written;
  // Issue the signal when the callback is destroyed rather than in the callback because the
  // callback won't get called in some error paths.
  auto data_written_finished = fit::defer([&] { sync_completion_signal(&data_written); });
  auto write_all_data = streamer.Flush().then(
      [&data_status, data_written_finished = std::move(data_written_finished)](
          const fpromise::result<void, zx_status_t>& result) {
        data_status = result.is_ok() ? ZX_OK : result.error();
        return result;
      });

  // Discard things we don't need any more. This has to be after the Flush call above to ensure
  // all data has been copied from these buffers.
  data_mapping.Unmap();
  unpaged_backing_data_.reset();

  // FreePagedVmo() will return the reference that keeps this object alive on behalf of the paging
  // system so we can free it outside the lock. However, when a Blob is being written it can't be
  // mapped so we know there should be no pager reference. Otherwise, calling FreePagedVmo() will
  // make future uses of the mapped data go invalid.
  //
  // If in the future we need to support memory mapping a paged VMO (like we allow mapping and using
  // the portions of a blob that are already known), then this code will have to be changed to not
  // free the VMO here (which will in turn require other changes).
  fbl::RefPtr<fs::Vnode> pager_reference = FreePagedVmo();
  ZX_DEBUG_ASSERT(!pager_reference);

  write_info_->compressor.reset();

  // Wrap all pending writes with a strong reference to this Blob, so that it stays
  // alive while there are writes in progress acting on it.
  BlobTransaction transaction;
  if (zx_status_t status = WriteMetadata(transaction, compression_algorithm); status != ZX_OK) {
    return status;
  }
  if (write_info_->old_blob) {
    zx_status_t status = blobfs_->FreeInode(write_info_->old_blob->Ino(), transaction);
    ZX_ASSERT_MSG(status == ZX_OK, "FreeInode failed with error: %s", zx_status_get_string(status));
    auto& cache = GetCache();
    status = cache.Evict(write_info_->old_blob);
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to evict old blob with error: %s",
                  zx_status_get_string(status));
    status = cache.Add(fbl::RefPtr(this));
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to add blob to cache with error: %s",
                  zx_status_get_string(status));
  }
  transaction.Commit(*blobfs_->GetJournal(), std::move(write_all_data),
                     [self = fbl::RefPtr(this)]() {});

  // It's not safe to continue until all data has been written because we might need to reload it
  // (e.g. if the blob is immediately read after writing), and the journal caches data in ring
  // buffers, so wait until that has happened.  We don't need to wait for the metadata because we
  // cache that.
  sync_completion_wait(&data_written, ZX_TIME_INFINITE);
  if (data_status != ZX_OK) {
    return data_status;
  }

  blobfs_->GetMetrics()->UpdateClientWrite(block_count_ * kBlobfsBlockSize, merkle_size,
                                           ticker.End(), generation_time);
  return MarkReadable(compression_algorithm);
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
          ZX_ASSERT_MSG(!data->empty(), "Data span for writing should not be empty.");
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

zx_status_t Blob::MarkReadable(CompressionAlgorithm compression_algorithm) {
  if (readable_event_.is_valid()) {
    zx_status_t status = readable_event_.signal(0u, ZX_USER_SIGNAL_0);
    if (status != ZX_OK) {
      MarkError();
      return status;
    }
  }
  set_state(BlobState::kReadable);
  syncing_state_ = SyncingState::kSyncing;
  write_info_.reset();
  return ZX_OK;
}

void Blob::MarkError() {
  if (state_ != BlobState::kError) {
    if (zx_status_t status = GetCache().Evict(fbl::RefPtr(this)); status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to evict blob from cache";
    }
    set_state(BlobState::kError);
  }
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

  if (state_ != BlobState::kReadable) {
    return ZX_ERR_BAD_STATE;
  }
  if (blob_size_ == 0) {
    return ZX_ERR_BAD_STATE;
  }

  auto status = LoadVmosFromDisk();
  if (status != ZX_OK) {
    return status;
  }

  zx::vmo clone;
  status = paged_vmo().create_child(ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE, 0, blob_size_, &clone);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create child VMO: " << zx_status_get_string(status);
    return status;
  }
  DidClonePagedVmo();

  // Only add exec right to VMO if explictly requested.  (Saves a syscall if we're just going to
  // drop the right back again in replace() call below.)
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
  *out_size = blob_size_;

  return ZX_OK;
}

zx_status_t Blob::ReadInternal(void* data, size_t len, size_t off, size_t* actual) {
  TRACE_DURATION("blobfs", "Blobfs::ReadInternal", "len", len, "off", off);

  // The common case is that the blob is already loaded. To allow multiple readers, it's important
  // to avoid taking an exclusive lock unless necessary.
  fs::SharedLock lock(mutex_);

  // Only expect this to be called when the blob is open. The fidl API guarantees this but tests
  // can easily forget to open the blob before trying to read.
  ZX_DEBUG_ASSERT(open_count() > 0);

  if (state_ != BlobState::kReadable)
    return ZX_ERR_BAD_STATE;

  if (!IsDataLoaded()) {
    // Release the shared lock and load the data from within an exclusive lock. LoadVmosFromDisk()
    // can be called multiple times so the race condition caused by this unlocking will be benign.
    lock.unlock();
    {
      // Load the VMO data from within the lock.
      std::lock_guard exclusive_lock(mutex_);
      if (zx_status_t status = LoadVmosFromDisk(); status != ZX_OK)
        return status;
    }
    lock.lock();

    // The readable state should never change (from the value we checked at the top of this
    // function) by attempting to load from disk, that only happens when we try to write.
    ZX_DEBUG_ASSERT(state_ == BlobState::kReadable);
  }

  if (blob_size_ == 0) {
    *actual = 0;
    return ZX_OK;
  }
  if (off >= blob_size_) {
    *actual = 0;
    return ZX_OK;
  }
  if (len > (blob_size_ - off)) {
    len = blob_size_ - off;
  }
  ZX_DEBUG_ASSERT(IsDataLoaded());

  // Send reads through the pager. This will potentially page-in the data by reentering us from the
  // kernel on the pager thread.
  ZX_DEBUG_ASSERT(paged_vmo().is_valid());
  if (zx_status_t status = paged_vmo().read(data, off, len); status != ZX_OK)
    return status;
  *actual = len;
  return ZX_OK;
}

zx_status_t Blob::LoadPagedVmosFromDisk() {
  ZX_ASSERT_MSG(!IsDataLoaded(), "Data VMO is not loaded.");

  // If there is an overridden cache policy for pager-backed blobs, apply it now. Otherwise the
  // system-wide default will be used.
  std::optional<CachePolicy> cache_policy = blobfs_->pager_backed_cache_policy();
  if (cache_policy) {
    set_overridden_cache_policy(*cache_policy);
  }

  zx::status<LoaderInfo> load_info_or =
      blobfs_->loader().LoadBlob(map_index_, &blobfs_->blob_corruption_notifier());
  if (load_info_or.is_error())
    return load_info_or.error_value();

  // Make the vmo.
  if (auto status = EnsureCreatePagedVmo(load_info_or->layout->FileBlockAlignedSize());
      status.is_error())
    return status.error_value();

  // Commit the other load information.
  loader_info_ = std::move(*load_info_or);

  return ZX_OK;
}

zx_status_t Blob::LoadVmosFromDisk() {
  // We expect the file to be open in FIDL for this to be called. Whether the paged vmo is
  // registered with the pager is dependent on the HasReferences() flag so this should not get
  // out-of-sync.
  ZX_DEBUG_ASSERT(HasReferences());

  if (IsDataLoaded())
    return ZX_OK;

  if (blob_size_ == 0) {
    // Null blobs don't need any loading, just verification that they're correct.
    return VerifyNullBlob();
  }

  zx_status_t status = LoadPagedVmosFromDisk();
  if (status == ZX_OK)
    SetPagedVmoName(true);

  syncing_state_ = SyncingState::kDone;
  return status;
}

zx_status_t Blob::PrepareDataVmoForWriting() {
  if (IsDataLoaded())
    return ZX_OK;

  uint64_t block_aligned_size = fbl::round_up(blob_size_, kBlobfsBlockSize);
  if (zx_status_t status = zx::vmo::create(block_aligned_size, 0, &unpaged_backing_data_);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create data vmo: " << zx_status_get_string(status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Blob::QueueUnlink() {
  std::lock_guard lock(mutex_);

  deletable_ = true;
  // Attempt to purge in case the blob has been unlinked with no open fds
  return TryPurge();
}

zx_status_t Blob::Verify() {
  {
    std::lock_guard lock(mutex_);
    if (auto status = LoadVmosFromDisk(); status != ZX_OK)
      return status;
  }

  // For non-pager-backed blobs, commit the entire blob in memory. This will cause all of the pages
  // to be verified as they are read in (or for the null bob we just verify immediately). If the
  // commit operation fails due to a verification failure, we do propagate the error back via the
  // return status.
  //
  // This is a read-only operation on the blob so can be done with the shared lock. Since it will
  // reenter the Blob object on the pager thread to satisfy this request, it actually MUST be done
  // with only the shared lock or the reentrance on the pager thread will deadlock us.
  {
    fs::SharedLock lock(mutex_);

    // There is a race condition if somehow this blob was unloaded in between the above exclusive
    // lock and the shared lock in this block. Currently this is not possible because there is only
    // one thread processing fidl messages and paging events on the pager threads can't unload the
    // blob.
    //
    // But in the future certain changes might make this theoretically possible (though very
    // difficult to imagine in practice). If this were to happen, we would prefer to err on the side
    // of reporting a blob valid rather than mistakenly reporting errors that might cause a valid
    // blob to be deleted.
    if (state_ != BlobState::kReadable)
      return ZX_OK;

    if (blob_size_ == 0) {
      // It's the null blob, so just verify.
      return VerifyNullBlob();
    }
    return paged_vmo().op_range(ZX_VMO_OP_COMMIT, 0, blob_size_, nullptr, 0);
  }
}

void Blob::OnNoPagedVmoClones() {
  // Override the default behavior of PagedVnode to avoid clearing the paged_vmo. We keep this
  // alive for caching purposes as long as this object is alive, and this object's lifetime is
  // managed by the BlobCache.
  if (!HasReferences()) {
    // Mark the name to help identify the VMO is unused.
    SetPagedVmoName(false);

    // This might have been the last reference to a deleted blob, so try purging it.
    if (zx_status_t status = TryPurge(); status != ZX_OK) {
      FX_LOGS(WARNING) << "Purging blob " << digest()
                       << " failed: " << zx_status_get_string(status);
    }
  }
}

BlobCache& Blob::GetCache() { return blobfs_->GetCache(); }

bool Blob::ShouldCache() const {
  std::lock_guard lock(mutex_);
  return state() == BlobState::kReadable;
}

void Blob::ActivateLowMemory() {
  // The reference returned by FreePagedVmo() needs to be released outside of the lock since it
  // could be keeping this class in scope.
  fbl::RefPtr<fs::Vnode> pager_reference;
  {
    std::lock_guard lock(mutex_);

    // We shouldn't be putting the blob into a low-memory state while it is still mapped.
    //
    // It is common for tests to trigger this assert during Blobfs tear-down. This will happen when
    // the "no clones" message was not delivered before destruction. This can happen if the test
    // code kept a vmo reference, but can also happen when there are no clones because the delivery
    // of this message depends on running the message loop which is easy to skip in a test.
    //
    // Often, the solution is to call RunUntilIdle() on the loop after the test code has cleaned up
    // its mappings but before deleting Blobfs. This will allow the pending notifications to be
    // delivered.
    ZX_ASSERT_MSG(!has_clones(), "Cannot put blob in low memory state as its mapped via clones.");

    pager_reference = FreePagedVmo();

    unpaged_backing_data_.reset();
    loader_info_ = LoaderInfo();  // Release the verifiers and associated Merkle data.
  }
  // When the pager_reference goes out of scope here, it could delete |this|.
}

Blob::~Blob() { ActivateLowMemory(); }

fs::VnodeProtocolSet Blob::GetProtocols() const { return fs::VnodeProtocol::kFile; }

bool Blob::ValidateRights(fs::Rights rights) const {
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
  auto event = blobfs_->GetMetrics()->NewLatencyEvent(fs_metrics::Event::kRead);

  return ReadInternal(data, len, off, out_actual);
}

zx_status_t Blob::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
  TRACE_DURATION("blobfs", "Blob::Write", "len", len, "off", offset);

  std::lock_guard lock(mutex_);
  auto event = blobfs_->GetMetrics()->NewLatencyEvent(fs_metrics::Event::kWrite);
  return WriteInternal(data, len, {offset}, out_actual);
}

zx_status_t Blob::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
  auto event = blobfs_->GetMetrics()->NewLatencyEvent(fs_metrics::Event::kAppend);

  std::lock_guard lock(mutex_);

  zx_status_t status = WriteInternal(data, len, std::nullopt, out_actual);
  if (state() == BlobState::kDataWrite) {
    ZX_DEBUG_ASSERT(write_info_ != nullptr);
    *out_end = write_info_->bytes_written;
  } else {
    *out_end = blob_size_;
  }
  return status;
}

zx_status_t Blob::GetAttributes(fs::VnodeAttributes* a) {
  auto event = blobfs_->GetMetrics()->NewLatencyEvent(fs_metrics::Event::kGetAttr);

  // SizeData() expects to be called outside the lock.
  auto content_size = SizeData();

  std::lock_guard lock(mutex_);

  *a = fs::VnodeAttributes();
  a->mode = V_TYPE_FILE | V_IRUSR | V_IXUSR;
  a->inode = map_index_;
  a->content_size = content_size;
  a->storage_size = block_count_ * kBlobfsBlockSize;
  a->link_count = 1;
  a->creation_time = 0;
  a->modification_time = 0;
  return ZX_OK;
}

zx_status_t Blob::Truncate(size_t len) {
  TRACE_DURATION("blobfs", "Blob::Truncate", "len", len);
  auto event = blobfs_->GetMetrics()->NewLatencyEvent(fs_metrics::Event::kTruncate);
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

  // Only expect this to be called when the blob is open. The fidl API guarantees this but tests
  // can easily forget to open the blob before getting the VMO.
  ZX_DEBUG_ASSERT(open_count() > 0);

  if (flags & fuchsia_io::wire::kVmoFlagWrite) {
    return ZX_ERR_NOT_SUPPORTED;
  } else if (flags & fuchsia_io::wire::kVmoFlagExact) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Let clients map and set the names of their VMOs.
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHTS_PROPERTY;
  // We can ignore fuchsia_io_VMO_FLAG_PRIVATE, since private / shared access to the underlying VMO
  // can both be satisfied with a clone due to the immutability of blobfs blobs.
  rights |= (flags & fuchsia_io::wire::kVmoFlagRead) ? ZX_RIGHT_READ : 0;
  rights |= (flags & fuchsia_io::wire::kVmoFlagExec) ? ZX_RIGHT_EXECUTE : 0;
  return CloneDataVmo(rights, out_vmo, out_size);
}

#endif  // defined(__Fuchsia__)

void Blob::Sync(SyncCallback on_complete) {
  // This function will issue its callbacks on either the current thread or the journal thread. The
  // vnode interface says this is OK.
  TRACE_DURATION("blobfs", "Blob::Sync");
  auto event = blobfs_->GetMetrics()->NewLatencyEvent(fs_metrics::Event::kSync);

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

// This function will get called on an arbitrary pager worker thread.
void Blob::VmoRead(uint64_t offset, uint64_t length) {
  TRACE_DURATION("blobfs", "Blob::VmoRead", "offset", offset, "length", length);

  // It's important that this function use only a shared read lock. This is for performance (to
  // allow multiple page requests to be run in parallel) and to prevent deadlock with the non-paged
  // Read() path. The non-paged path is implemented by reading from the vmo which will recursively
  // call into this code and taking an exclusive lock would deadlock.
  fs::SharedLock lock(mutex_);

  if (!paged_vmo()) {
    // Races with calling FreePagedVmo() on another thread can result in stale read requests. Ignore
    // them if the VMO is gone.
    return;
  }

  ZX_DEBUG_ASSERT(IsDataLoaded());

  if (is_corrupt_) {
    FX_LOGS(ERROR) << "Blobfs failing page request because blob was previously found corrupt.";
    if (auto error_result =
            paged_vfs()->ReportPagerError(paged_vmo(), offset, length, ZX_ERR_BAD_STATE);
        error_result.is_error()) {
      FX_LOGS(ERROR) << "Failed to report pager error to kernel: " << error_result.status_string();
    }
    return;
  }

  auto page_supplier = PageLoader::PageSupplier(
      [paged_vfs = paged_vfs(), dest_vmo = &paged_vmo()](
          uint64_t offset, uint64_t length, const zx::vmo& aux_vmo, uint64_t aux_offset) {
        return paged_vfs->SupplyPages(*dest_vmo, offset, length, aux_vmo, aux_offset);
      });
  PagerErrorStatus pager_error_status =
      blobfs_->page_loader().TransferPages(std::move(page_supplier), offset, length, loader_info_);
  if (pager_error_status != PagerErrorStatus::kOK) {
    FX_LOGS(ERROR) << "Pager failed to transfer pages to the blob, error: "
                   << zx_status_get_string(static_cast<zx_status_t>(pager_error_status));
    if (auto error_result = paged_vfs()->ReportPagerError(
            paged_vmo(), offset, length, static_cast<zx_status_t>(pager_error_status));
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
    if (pager_error_status == PagerErrorStatus::kErrDataIntegrity)
      is_corrupt_ = true;
  }
}

bool Blob::HasReferences() const { return open_count() > 0 || has_clones(); }

void Blob::CompleteSync() {
  // Called on the journal thread when the syncing is complete.
  {
    std::scoped_lock guard(mutex_);
    syncing_state_ = SyncingState::kDone;
  }
}

void Blob::WillTeardownFilesystem() {
  // Be careful to release the pager reference outside the lock.
  fbl::RefPtr<fs::Vnode> pager_reference;
  {
    std::lock_guard lock(mutex_);
    pager_reference = FreePagedVmo();
  }
  // When pager_reference goes out of scope here, it could cause |this| to be deleted.
}

zx_status_t Blob::OpenNode([[maybe_unused]] ValidatedOptions options,
                           fbl::RefPtr<Vnode>* out_redirect) {
  std::lock_guard lock(mutex_);
  if (IsDataLoaded() && open_count() == 1) {
    // Just went from an unopened node that already had data to an opened node (the open_count()
    // reflects the new state).
    //
    // This normally means that the node was closed but cached, and we're not re-opening it. This
    // means we have to mark things as being open and register for the corresponding notifications.
    //
    // It's also possible to get in this state if there was a memory mapping for a file that
    // was otherwise closed. In that case we don't need to do anything but the operations here
    // can be performed multiple times with no bad effects. Avoiding these calls in the "mapped but
    // opened" state would mean checking for no mappings which bundles this code more tightly to
    // the HasReferences() implementation that is better avoided.
    SetPagedVmoName(true);
  }
  return ZX_OK;
}

zx_status_t Blob::CloseNode() {
  std::lock_guard lock(mutex_);

  auto event = blobfs_->GetMetrics()->NewLatencyEvent(fs_metrics::Event::kClose);

  if (paged_vmo() && !HasReferences())
    SetPagedVmoName(false);

  // Attempt purge in case blob was unlinked prior to close.
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

  if (state_ == BlobState::kReadable) {
    // A readable blob should only be purged if it has been unlinked.
    ZX_ASSERT_MSG(deletable_, "Should not purge blob which is not unlinked.");

    BlobTransaction transaction;
    if (zx_status_t status = blobfs_->FreeInode(map_index_, transaction); status != ZX_OK)
      return status;
    transaction.Commit(*blobfs_->GetJournal());
  }

  // If the blob is in the error state, it should have already been evicted from
  // the cache (see MarkError).
  if (state_ != BlobState::kError) {
    if (zx_status_t status = GetCache().Evict(fbl::RefPtr(this)); status != ZX_OK)
      return status;
  }

  set_state(BlobState::kPurged);
  return ZX_OK;
}

uint32_t Blob::GetBlockSize() const { return blobfs_->Info().block_size; }

void Blob::SetPagedVmoName(bool active) {
  fbl::StringBuffer<ZX_MAX_NAME_LEN> name;
  if (active) {
    FormatBlobDataVmoName(digest(), &name);
  } else {
    FormatInactiveBlobDataVmoName(digest(), &name);
  }
  // Ignore failures, the name is for informational purposes only.
  paged_vmo().set_property(ZX_PROP_NAME, name.data(), name.size());
}

}  // namespace blobfs
