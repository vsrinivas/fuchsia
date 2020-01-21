// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob.h"

#include <assert.h>
#include <ctype.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/sync/completion.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/vfs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>
#include <vector>

#include <digest/digest.h>
#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fs/journal/data_streamer.h>
#include <fs/metrics/events.h>
#include <fs/transaction/writeback.h>
#include <fs/vfs_types.h>

#include "blobfs.h"
#include "compression/lz4.h"
#include "compression/zstd-plain.h"
#include "compression/zstd-rac.h"
#include "iterator/allocated-extent-iterator.h"
#include "iterator/block-iterator.h"
#include "iterator/extent-iterator.h"
#include "iterator/node-populator.h"
#include "iterator/vector-extent-iterator.h"
#include "metrics.h"

namespace blobfs {
namespace {

using digest::Digest;
using digest::MerkleTreeCreator;

// Blob's vmo names have following pattern
// "blob-1abc8" or "compressedBlob-5c"
constexpr char kBlobVmoNamePrefix[] = "blob";
constexpr char kCompressedBlobVmoNamePrefix[] = "compressedBlob";

void FormatVmoName(const char* prefix, fbl::StringBuffer<ZX_MAX_NAME_LEN>* vmo_name, size_t index) {
  vmo_name->Clear();
  vmo_name->AppendPrintf("%s-%lx", prefix, index);
}

}  // namespace

zx_status_t Blob::Verify() const {
  TRACE_DURATION("blobfs", "Blobfs::Verify");
  fs::Ticker ticker(blobfs_->Metrics().Collecting());

  const void* data = inode_.blob_size ? GetData() : nullptr;
  const void* tree = inode_.blob_size ? GetMerkle() : nullptr;
  const uint64_t data_size = inode_.blob_size;

  // TODO(smklein): We could lazily verify more of the VMO if
  // we could fault in pages on-demand.
  //
  // For now, we aggressively verify the entire VMO up front.
  MerkleTreeVerifier mtv;
  zx_status_t status = mtv.SetDataLength(data_size);
  size_t merkle_size = mtv.GetTreeLength();
  if (status != ZX_OK ||
      (status = mtv.SetTree(tree, merkle_size, GetKey(), digest::kSha256Length)) != ZX_OK ||
      (status = mtv.Verify(data, data_size, 0)) != ZX_OK) {
    Digest digest(GetKey());
    FS_TRACE_ERROR("blobfs verify(%s) Failure: %s\n", digest.ToString().c_str(),
                   zx_status_get_string(status));
  }
  blobfs_->Metrics().UpdateMerkleVerify(data_size, merkle_size, ticker.End());

  return status;
}

zx_status_t Blob::InitMerkleTreeVerifier(std::unique_ptr<MerkleTreeVerifier>* verifier) {
  // Pre-populate the Merkle tree blocks. Verification takes place on the page fault path, so we
  // can't block to fault in the Merkle tree then.
  zx_status_t status = blobfs_->TransferPagesToVmo(
      GetMapIndex(), 0, MerkleTreeBlocks(inode_) * kBlobfsBlockSize, mapping_.vmo(), nullptr);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to page in Merkle tree blocks: %s\n", zx_status_get_string(status));
    return status;
  }

  const void* tree = inode_.blob_size ? GetMerkle() : nullptr;
  auto merkle_tree_verifier = std::make_unique<MerkleTreeVerifier>();

  status = merkle_tree_verifier->SetDataLength(inode_.blob_size);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to set data length for Merkle tree verifier: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  size_t merkle_size = merkle_tree_verifier->GetTreeLength();
  status = merkle_tree_verifier->SetTree(tree, merkle_size, GetKey(), digest::kSha256Length);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to set tree for Merkle tree verifier: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  *verifier = std::move(merkle_tree_verifier);
  return ZX_OK;
}

zx_status_t Blob::InitVmos() {
  TRACE_DURATION("blobfs", "Blobfs::InitVmos");

  if (mapping_.vmo()) {
    return ZX_OK;
  }

  uint64_t data_blocks = BlobDataBlocks(inode_);
  uint64_t merkle_blocks = MerkleTreeBlocks(inode_);
  uint64_t num_blocks = data_blocks + merkle_blocks;

  if (num_blocks == 0) {
    // No need to initialize VMO for null blob.
    return ZX_OK;
  }

  // Reverts blob back to uninitialized state on error.
  auto cleanup = fbl::MakeAutoCall([this]() { BlobCloseHandles(); });

  size_t vmo_size;
  if (mul_overflow(num_blocks, kBlobfsBlockSize, &vmo_size)) {
    FS_TRACE_ERROR("Multiplication overflow");
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::StringBuffer<ZX_MAX_NAME_LEN> vmo_name;
  FormatVmoName(kBlobVmoNamePrefix, &vmo_name, Ino());

  // Use the pager only if the blob is uncompressed AND blobfs has a pager set up.
  bool use_pager = (((inode_.header.flags & (kBlobFlagLZ4Compressed | kBlobFlagZSTDCompressed |
                                             kBlobFlagZSTDSeekableCompressed)) == 0) &&
                    blobfs_->PagingEnabled());

  zx_status_t status;
  if (use_pager) {
    page_watcher_ = std::make_unique<PageWatcher>(blobfs_, GetMapIndex());
    zx::vmo vmo;
    status = page_watcher_->CreatePagedVmo(vmo_size, &vmo);
    if (status != ZX_OK) {
      return status;
    }

    vmo.set_property(ZX_PROP_NAME, vmo_name.c_str(), vmo_name.length());
    status = mapping_.Map(std::move(vmo));
  } else {
    status = mapping_.CreateAndMap(vmo_size, vmo_name.c_str());
  }

  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to initialize vmo; error: %s\n", zx_status_get_string(status));
    return status;
  }
  if ((status = blobfs_->AttachVmo(mapping_.vmo(), &vmoid_)) != ZX_OK) {
    FS_TRACE_ERROR("Failed to attach VMO to block device; error: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  if (use_pager) {
    std::unique_ptr<MerkleTreeVerifier> verifier = nullptr;
    status = InitMerkleTreeVerifier(&verifier);
    if (status != ZX_OK) {
      return status;
    }
    auto verifier_info = std::make_unique<VerifierInfo>();
    verifier_info->verifier = std::move(verifier);
    verifier_info->verifier_data_length = inode_.blob_size;
    page_watcher_->SetPageVerifierInfo(std::move(verifier_info));

  } else if ((inode_.header.flags & kBlobFlagLZ4Compressed) != 0) {
    status = InitCompressed(CompressionAlgorithm::LZ4);
  } else if ((inode_.header.flags & kBlobFlagZSTDCompressed) != 0) {
    status = InitCompressed(CompressionAlgorithm::ZSTD);
  } else if ((inode_.header.flags & kBlobFlagZSTDSeekableCompressed) != 0) {
    status = InitCompressed(CompressionAlgorithm::ZSTD_SEEKABLE);
  } else {
    status = InitUncompressed();
  }

  if (status != ZX_OK) {
    return status;
  }

  // Verify the blob up front if the pager is not enabled. If the pager is enabled, the page request
  // handler verifies pages as they are read in from disk.
  if (!use_pager) {
    status = Verify();
    if (status != ZX_OK) {
      return status;
    }
  }

  cleanup.cancel();
  return ZX_OK;
}

zx_status_t Blob::InitCompressed(CompressionAlgorithm algorithm) {
  TRACE_DURATION("blobfs", "Blobfs::InitCompressed", "size", inode_.blob_size, "blocks",
                 inode_.block_count);
  fs::Ticker ticker(blobfs_->Metrics().Collecting());
  fs::ReadTxn txn(blobfs_);
  uint32_t merkle_blocks = MerkleTreeBlocks(inode_);

  fzl::OwnedVmoMapper compressed_mapper;
  uint32_t compressed_blocks = (inode_.block_count - merkle_blocks);
  size_t compressed_size;
  if (mul_overflow(compressed_blocks, kBlobfsBlockSize, &compressed_size)) {
    FS_TRACE_ERROR("Multiplication overflow\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::StringBuffer<ZX_MAX_NAME_LEN> vmo_name;
  FormatVmoName(kCompressedBlobVmoNamePrefix, &vmo_name, Ino());
  zx_status_t status = compressed_mapper.CreateAndMap(compressed_size, vmo_name.c_str());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to initialized compressed vmo; error: %d\n", status);
    return status;
  }
  vmoid_t compressed_vmoid;
  status = blobfs_->AttachVmo(compressed_mapper.vmo(), &compressed_vmoid);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to attach compressed VMO to blkdev: %d\n", status);
    return status;
  }

  auto detach =
      fbl::MakeAutoCall([this, &compressed_vmoid]() { blobfs_->DetachVmo(compressed_vmoid); });

  const uint64_t kDataStart = DataStartBlock(blobfs_->Info());
  AllocatedExtentIterator extent_iter(blobfs_->GetNodeFinder(), GetMapIndex());
  BlockIterator block_iter(&extent_iter);

  // Read the uncompressed merkle tree into the start of the blob's VMO.
  status = StreamBlocks(&block_iter, merkle_blocks,
                        [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
                          txn.Enqueue(vmoid_, vmo_offset, dev_offset + kDataStart, length);
                          return ZX_OK;
                        });
  if (status != ZX_OK) {
    return status;
  }

  // Read the compressed blocks into the compressed VMO, accounting for the merkle blocks
  // which have already been seen.
  ZX_DEBUG_ASSERT(block_iter.BlockIndex() == merkle_blocks);

  status = StreamBlocks(&block_iter, compressed_blocks,
                        [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
                          txn.Enqueue(compressed_vmoid, vmo_offset - merkle_blocks,
                                      dev_offset + kDataStart, length);
                          return ZX_OK;
                        });

  if (status != ZX_OK) {
    return status;
  }

  if ((status = txn.Transact()) != ZX_OK) {
    FS_TRACE_ERROR("Failed to flush read transaction: %d\n", status);
    return status;
  }

  fs::Duration read_time = ticker.End();
  ticker.Reset();

  // Decompress the compressed data into the target buffer.
  size_t target_size = inode_.blob_size;
  const void* compressed_buffer = compressed_mapper.start();
  switch (algorithm) {
    case CompressionAlgorithm::LZ4:
      status = LZ4Decompress(GetData(), &target_size, compressed_buffer, &compressed_size);
      break;
    case CompressionAlgorithm::ZSTD:
      status = ZSTDDecompress(GetData(), &target_size, compressed_buffer, &compressed_size);
      break;
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      // TODO(markdittmer): This does not have the same signature as other decompression routines.
      status = ZSTDSeekableDecompress(GetData(), &target_size, compressed_buffer);
      break;
    default:
      FS_TRACE_ERROR("Unsupported decompression algorithm");
      return ZX_ERR_NOT_SUPPORTED;
  }

  if (status != ZX_OK) {
    FS_TRACE_ERROR("Failed to decompress data: %d\n", status);
    return status;
  } else if (target_size != inode_.blob_size) {
    FS_TRACE_ERROR("Failed to fully decompress blob (%zu of %zu expected)\n", target_size,
                   inode_.blob_size);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  blobfs_->Metrics().UpdateMerkleDecompress(compressed_blocks * kBlobfsBlockSize, inode_.blob_size,
                                            read_time, ticker.End());
  return ZX_OK;
}

zx_status_t Blob::InitUncompressed() {
  TRACE_DURATION("blobfs", "Blobfs::InitUncompressed", "size", inode_.blob_size, "blocks",
                 inode_.block_count);
  fs::Ticker ticker(blobfs_->Metrics().Collecting());
  fs::ReadTxn txn(blobfs_);
  AllocatedExtentIterator extent_iter(blobfs_->GetNodeFinder(), GetMapIndex());
  BlockIterator block_iter(&extent_iter);
  // Read both the uncompressed merkle tree and data.
  const uint64_t blob_data_blocks = BlobDataBlocks(inode_);
  const uint64_t merkle_blocks = MerkleTreeBlocks(inode_);
  if (blob_data_blocks + merkle_blocks > std::numeric_limits<uint32_t>::max()) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  const uint32_t length = static_cast<uint32_t>(blob_data_blocks + merkle_blocks);
  const uint64_t data_start = DataStartBlock(blobfs_->Info());
  zx_status_t status = StreamBlocks(
      &block_iter, length, [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
        txn.Enqueue(vmoid_, vmo_offset, dev_offset + data_start, length);
        return ZX_OK;
      });

  if (status != ZX_OK) {
    return status;
  }

  status = txn.Transact();
  if (status != ZX_OK) {
    return status;
  }
  blobfs_->Metrics().UpdateMerkleDiskRead(length * kBlobfsBlockSize, ticker.End());
  return status;
}

void Blob::PopulateInode(uint32_t node_index) {
  ZX_DEBUG_ASSERT(map_index_ == 0);
  SetState(kBlobStateReadable);
  map_index_ = node_index;
  Inode* inode = blobfs_->GetNode(node_index);
  inode_ = *inode;
}

uint64_t Blob::SizeData() const {
  if (GetState() == kBlobStateReadable) {
    return inode_.blob_size;
  }
  return 0;
}

Blob::Blob(Blobfs* bs, const Digest& digest)
    : CacheNode(digest),
      blobfs_(bs),
      flags_(kBlobStateEmpty),
      syncing_(false),
      clone_watcher_(this) {}

void Blob::BlobCloseHandles() {
  page_watcher_.reset();
  mapping_.Reset();
  readable_event_.reset();
}

zx_status_t Blob::SpaceAllocate(uint64_t size_data) {
  TRACE_DURATION("blobfs", "Blobfs::SpaceAllocate", "size_data", size_data);
  fs::Ticker ticker(blobfs_->Metrics().Collecting());

  if (GetState() != kBlobStateEmpty) {
    return ZX_ERR_BAD_STATE;
  }

  auto write_info = std::make_unique<WritebackInfo>();

  // Initialize the inode with known fields.
  memset(inode_.merkle_root_hash, 0, sizeof(inode_.merkle_root_hash));
  inode_.blob_size = size_data;
  inode_.block_count = MerkleTreeBlocks(inode_) + static_cast<uint32_t>(BlobDataBlocks(inode_));

  // Special case for the null blob: We skip the write phase.
  if (inode_.blob_size == 0) {
    zx_status_t status = blobfs_->GetAllocator()->ReserveNodes(1, &write_info->node_indices);
    if (status != ZX_OK) {
      return status;
    }
    map_index_ = write_info->node_indices[0].index();
    write_info_ = std::move(write_info);

    if ((status = Verify()) != ZX_OK) {
      return status;
    }
    SetState(kBlobStateDataWrite);

    blobfs_->journal()->schedule_task(
        WriteMetadata().and_then([blob = fbl::RefPtr(this)]() { blob->CompleteSync(); }));
    return ZX_OK;
  }

  fbl::Vector<ReservedExtent> extents;
  fbl::Vector<ReservedNode> nodes;

  // Reserve space for the blob.
  zx_status_t status = blobfs_->GetAllocator()->ReserveBlocks(inode_.block_count, &extents);
  if (status != ZX_OK) {
    return status;
  }
  if (extents.size() > kMaxBlobExtents) {
    FS_TRACE_ERROR("Error: Block reservation requires too many extents (%zu vs %zu max)\n",
                   extents.size(), kMaxBlobExtents);
    return ZX_ERR_BAD_STATE;
  }
  const ExtentCountType extent_count = static_cast<ExtentCountType>(extents.size());

  // Reserve space for all the nodes necessary to contain this blob.
  size_t node_count = NodePopulator::NodeCountForExtents(extent_count);
  status = blobfs_->GetAllocator()->ReserveNodes(node_count, &nodes);
  if (status != ZX_OK) {
    return status;
  }

  fbl::StringBuffer<ZX_MAX_NAME_LEN> vmo_name;
  if (inode_.blob_size >= kCompressionMinBytesSaved) {
    // TODO(markdittmer): Lookup stored choice of compression algorithm here.
    write_info->compressor = BlobCompressor::Create(CompressionAlgorithm::ZSTD, inode_.blob_size);
    if (!write_info->compressor) {
      FS_TRACE_ERROR("blobfs: Failed to initialize compressor: %d\n", status);
      return status;
    }
  }

  // Open VMOs, so we can begin writing after allocate succeeds.
  fzl::OwnedVmoMapper mapping;
  FormatVmoName(kBlobVmoNamePrefix, &vmo_name, Ino());
  if ((status = mapping.CreateAndMap(inode_.block_count * kBlobfsBlockSize, vmo_name.c_str())) !=
      ZX_OK) {
    return status;
  }
  if ((status = blobfs_->AttachVmo(mapping.vmo(), &vmoid_)) != ZX_OK) {
    return status;
  }

  map_index_ = nodes[0].index();
  mapping_ = std::move(mapping);
  write_info->extents = std::move(extents);
  write_info->node_indices = std::move(nodes);
  write_info_ = std::move(write_info);

  SetState(kBlobStateDataWrite);
  blobfs_->Metrics().UpdateAllocation(size_data, ticker.End());
  return ZX_OK;
}

void* Blob::GetData() const {
  return fs::GetBlock(kBlobfsBlockSize, mapping_.start(), MerkleTreeBlocks(inode_));
}

void* Blob::GetMerkle() const { return mapping_.start(); }

fit::promise<void, zx_status_t> Blob::WriteMetadata() {
  TRACE_DURATION("blobfs", "Blobfs::WriteMetadata");
  assert(GetState() == kBlobStateDataWrite);

  // Update the on-disk hash.
  memcpy(inode_.merkle_root_hash, GetKey(), digest::kSha256Length);

  // All data has been written to the containing VMO.
  SetState(kBlobStateReadable);
  if (readable_event_.is_valid()) {
    zx_status_t status = readable_event_.signal(0u, ZX_USER_SIGNAL_0);
    if (status != ZX_OK) {
      SetState(kBlobStateError);
      return fit::make_error_promise(status);
    }
  }

  atomic_store(&syncing_, true);

  storage::UnbufferedOperationsBuilder operations;
  if (inode_.block_count) {
    // We utilize the NodePopulator class to take our reserved blocks and nodes and fill the
    // persistent map with an allocated inode / container.

    // If |on_node| is invoked on a node, it means that node was necessary to represent this
    // blob. Persist the node back to durable storge.
    auto on_node = [this, &operations](const ReservedNode& node) {
      blobfs_->PersistNode(node.index(), &operations);
    };

    // If |on_extent| is invoked on an extent, it was necessary to represent this blob. Persist
    // the allocation of these blocks back to durable storage.
    //
    // Additionally, because of the compression feature of blobfs, it is possible we reserved
    // more extents than this blob ended up using. Decrement |remaining_blocks| to track if we
    // should exit early.
    size_t remaining_blocks = inode_.block_count;
    auto on_extent = [this, &operations, &remaining_blocks](ReservedExtent& extent) {
      ZX_DEBUG_ASSERT(remaining_blocks > 0);
      if (remaining_blocks >= extent.extent().Length()) {
        // Consume the entire extent.
        remaining_blocks -= extent.extent().Length();
      } else {
        // Consume only part of the extent; we're done iterating.
        extent.SplitAt(static_cast<BlockCountType>(remaining_blocks));
        remaining_blocks = 0;
      }
      blobfs_->PersistBlocks(extent, &operations);
      if (remaining_blocks == 0) {
        return NodePopulator::IterationCommand::Stop;
      }
      return NodePopulator::IterationCommand::Continue;
    };

    Inode* mapped_inode = blobfs_->GetNode(map_index_);
    *mapped_inode = inode_;
    NodePopulator populator(blobfs_->GetAllocator(), std::move(write_info_->extents),
                            std::move(write_info_->node_indices));
    ZX_ASSERT(populator.Walk(on_node, on_extent) == ZX_OK);

    // Ensure all non-allocation flags are propagated to the inode.
    const uint16_t non_allocation_flags =
        kBlobFlagZSTDCompressed | kBlobFlagLZ4Compressed | kBlobFlagZSTDSeekableCompressed;
    mapped_inode->header.flags |= (inode_.header.flags & non_allocation_flags);
  } else {
    // Special case: Empty node.
    ZX_DEBUG_ASSERT(write_info_->node_indices.size() == 1);
    *(blobfs_->GetNode(map_index_)) = inode_;
    const ReservedNode& node = write_info_->node_indices[0];
    blobfs_->GetAllocator()->MarkInodeAllocated(node);
    blobfs_->PersistNode(node.index(), &operations);
  }

  write_info_.reset();

  return blobfs_->journal()
      ->WriteMetadata(operations.TakeOperations())
      .and_then([blob = fbl::RefPtr(this)]() { blob->CompleteSync(); });
}

zx_status_t Blob::WriteInternal(const void* data, size_t len, size_t* actual) {
  TRACE_DURATION("blobfs", "Blobfs::WriteInternal", "data", data, "len", len);

  *actual = 0;
  if (len == 0) {
    return ZX_OK;
  }

  const uint64_t data_start = DataStartBlock(blobfs_->Info());
  const uint32_t merkle_blocks = MerkleTreeBlocks(inode_);
  const size_t merkle_bytes = merkle_blocks * kBlobfsBlockSize;
  if (GetState() == kBlobStateDataWrite) {
    size_t to_write = fbl::min(len, inode_.blob_size - write_info_->bytes_written);
    size_t offset = write_info_->bytes_written + merkle_bytes;
    zx_status_t status = mapping_.vmo().write(data, offset, to_write);
    if (status != ZX_OK) {
      return status;
    }

    *actual = to_write;
    write_info_->bytes_written += to_write;

    if (write_info_->compressor) {
      if ((status = write_info_->compressor->Update(data, to_write)) != ZX_OK) {
        return status;
      }
      ConsiderCompressionAbort();
    }

    // More data to write.
    if (write_info_->bytes_written < inode_.blob_size) {
      return ZX_OK;
    }

    auto set_error = fbl::MakeAutoCall([this]() { SetState(kBlobStateError); });

    // Only write data to disk once we've buffered the file into memory.
    // This gives us a chance to try compressing the blob before we write it back.
    if (write_info_->compressor) {
      if ((status = write_info_->compressor->End()) != ZX_OK) {
        return status;
      }
      ConsiderCompressionAbort();
    }

    // Since the merkle tree and data are co-allocated, use a block iterator
    // to parse their data in order.
    VectorExtentIterator extent_iter(write_info_->extents);
    BlockIterator block_iter(&extent_iter);

    fs::Duration generation_time;
    std::vector<fit::promise<void, zx_status_t>> promises;
    fs::DataStreamer streamer(blobfs_->journal(), blobfs_->WritebackCapacity());

    MerkleTreeCreator mtc;
    if ((status = mtc.SetDataLength(inode_.blob_size)) != ZX_OK) {
      return status;
    }
    size_t merkle_size = mtc.GetTreeLength();
    if (merkle_size > 0) {
      // Tracking generation time.
      fs::Ticker ticker(blobfs_->Metrics().Collecting());

      // TODO(smklein): As an optimization, use the Append method to create the merkle tree as we
      // write data, rather than waiting until the data is fully downloaded to create the tree.
      uint8_t root[digest::kSha256Length];
      if ((status = mtc.SetTree(GetMerkle(), merkle_size, root, sizeof(root))) != ZX_OK ||
          (status = mtc.Append(GetData(), inode_.blob_size)) != ZX_OK) {
        return status;
      }

      Digest expected(GetKey());
      Digest actual(root);
      if (expected != actual) {
        // Downloaded blob did not match provided digest.
        return ZX_ERR_IO_DATA_INTEGRITY;
      }

      status = StreamBlocks(&block_iter, merkle_blocks,
                            [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
                              storage::UnbufferedOperation op = {
                                  .vmo = zx::unowned_vmo(mapping_.vmo().get()),
                                  {
                                      .type = storage::OperationType::kWrite,
                                      .vmo_offset = vmo_offset,
                                      .dev_offset = dev_offset + data_start,
                                      .length = length,
                                  }};
                              streamer.StreamData(std::move(op));
                              return ZX_OK;
                            });

      if (status != ZX_OK) {
        return status;
      }
      generation_time = ticker.End();
    } else if ((status = Verify()) != ZX_OK) {
      // Small blobs may not have associated Merkle Trees, and will
      // require validation, since we are not regenerating and checking
      // the digest.
      return status;
    }

    if (write_info_->compressor) {
      uint64_t blocks64 =
          fbl::round_up(write_info_->compressor->Size(), kBlobfsBlockSize) / kBlobfsBlockSize;
      ZX_DEBUG_ASSERT(blocks64 <= std::numeric_limits<uint32_t>::max());
      uint32_t blocks = static_cast<uint32_t>(blocks64);
      int64_t vmo_bias = -static_cast<int64_t>(merkle_blocks);
      ZX_DEBUG_ASSERT(block_iter.BlockIndex() + vmo_bias == 0);
      status = StreamBlocks(&block_iter, blocks,
                            [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
                              storage::UnbufferedOperation op = {
                                  .vmo = zx::unowned_vmo(write_info_->compressor->Vmo().get()),
                                  {
                                      .type = storage::OperationType::kWrite,
                                      .vmo_offset = vmo_offset - merkle_blocks,
                                      .dev_offset = dev_offset + data_start,
                                      .length = length,
                                  }};
                              streamer.StreamData(std::move(op));
                              return ZX_OK;
                            });

      if (status != ZX_OK) {
        return status;
      }
      blocks += MerkleTreeBlocks(inode_);
      // By compressing, we used less blocks than we originally reserved.
      ZX_DEBUG_ASSERT(inode_.block_count > blocks);

      inode_.block_count = blocks;
      // TODO(markdittmer): Use flag of chosen algorithm here.
      inode_.header.flags |= kBlobFlagZSTDCompressed;
    } else {
      uint64_t blocks64 = fbl::round_up(inode_.blob_size, kBlobfsBlockSize) / kBlobfsBlockSize;
      ZX_DEBUG_ASSERT(blocks64 <= std::numeric_limits<uint32_t>::max());
      uint32_t blocks = static_cast<uint32_t>(blocks64);
      status = StreamBlocks(
          &block_iter, blocks, [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
            storage::UnbufferedOperation op = {.vmo = zx::unowned_vmo(mapping_.vmo().get()),
                                               {
                                                   .type = storage::OperationType::kWrite,
                                                   .vmo_offset = vmo_offset,
                                                   .dev_offset = dev_offset + data_start,
                                                   .length = length,
                                               }};
            streamer.StreamData(std::move(op));
            return ZX_OK;
          });
      if (status != ZX_OK) {
        return status;
      }
    }

    // Enqueue the blob's final data work. Metadata must be enqueued separately.
    fs::Journal::Promise write_all_data = streamer.Flush();

    // No more data to write. Flush to disk.
    fs::Ticker ticker(blobfs_->Metrics().Collecting());  // Tracking enqueue time.

    // Wrap all pending writes with a strong reference to this Blob, so that it stays
    // alive while there are writes in progress acting on it.
    auto task = fs::wrap_reference(write_all_data.and_then(WriteMetadata()), fbl::RefPtr(this));
    blobfs_->journal()->schedule_task(std::move(task));
    blobfs_->Metrics().UpdateClientWrite(to_write, merkle_size, ticker.End(), generation_time);
    set_error.cancel();
    return ZX_OK;
  }
  return ZX_ERR_BAD_STATE;
}

void Blob::ConsiderCompressionAbort() {
  ZX_DEBUG_ASSERT(write_info_->compressor);
  if (inode_.blob_size - kCompressionMinBytesSaved < write_info_->compressor->Size()) {
    write_info_->compressor.reset();
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
    } else if (GetState() == kBlobStateReadable) {
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

zx_status_t Blob::CloneVmo(zx_rights_t rights, zx::vmo* out_vmo, size_t* out_size) {
  TRACE_DURATION("blobfs", "Blobfs::CloneVmo", "rights", rights);
  if (GetState() != kBlobStateReadable) {
    return ZX_ERR_BAD_STATE;
  }
  if (inode_.blob_size == 0) {
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status = InitVmos();
  if (status != ZX_OK) {
    return status;
  }

  const size_t merkle_bytes = MerkleTreeBlocks(inode_) * kBlobfsBlockSize;
  zx::vmo clone;

  zx_info_vmo_t info;
  status = mapping_.vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }
  if (info.flags & ZX_INFO_VMO_PAGER_BACKED) {
    status = mapping_.vmo().create_child(ZX_VMO_CHILD_PRIVATE_PAGER_COPY, merkle_bytes,
                                         inode_.blob_size, &clone);
  } else {
    status = mapping_.vmo().create_child(ZX_VMO_CHILD_COPY_ON_WRITE, merkle_bytes, inode_.blob_size,
                                         &clone);
  }
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to create child VMO: %s\n", zx_status_get_string(status));
    return status;
  }

  // Only add exec right to VMO if explictly requested.  (Saves a syscall if
  // we're just going to drop the right back again in replace() call below.)
  if (rights & ZX_RIGHT_EXECUTE) {
    if ((status = clone.replace_as_executable(zx::handle(), &clone)) != ZX_OK) {
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
    clone_watcher_.set_object(mapping_.vmo().get());
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
  ZX_DEBUG_ASSERT(status == ZX_OK);
  ZX_DEBUG_ASSERT((signal->observed & ZX_VMO_ZERO_CHILDREN) != 0);
  ZX_DEBUG_ASSERT(clone_watcher_.object() != ZX_HANDLE_INVALID);
  clone_watcher_.set_object(ZX_HANDLE_INVALID);
  clone_ref_ = nullptr;
}

zx_status_t Blob::ReadInternal(void* data, size_t len, size_t off, size_t* actual) {
  TRACE_DURATION("blobfs", "Blobfs::ReadInternal", "len", len, "off", off);

  if (GetState() != kBlobStateReadable) {
    return ZX_ERR_BAD_STATE;
  }

  if (inode_.blob_size == 0) {
    *actual = 0;
    return ZX_OK;
  }

  zx_status_t status = InitVmos();
  if (status != ZX_OK) {
    return status;
  }

  Digest d(GetKey());

  if (off >= inode_.blob_size) {
    *actual = 0;
    return ZX_OK;
  }
  if (len > (inode_.blob_size - off)) {
    len = inode_.blob_size - off;
  }

  const size_t merkle_bytes = MerkleTreeBlocks(inode_) * kBlobfsBlockSize;
  status = mapping_.vmo().read(data, merkle_bytes + off, len);
  if (status == ZX_OK) {
    *actual = len;
  }
  return status;
}

zx_status_t Blob::QueueUnlink() {
  flags_ |= kBlobFlagDeletable;
  // Attempt to purge in case the blob has been unlinked with no open fds
  return TryPurge();
}

zx_status_t Blob::VerifyBlob(Blobfs* bs, uint32_t node_index) {
  Inode* inode = bs->GetNode(node_index);
  Digest digest(inode->merkle_root_hash);
  fbl::RefPtr<Blob> vn = fbl::AdoptRef(new Blob(bs, digest));

  vn->PopulateInode(node_index);

  // If we are unable to read in the blob from disk, this should also be a VerifyBlob error.
  zx_status_t status = vn->InitVmos();
  if (status != ZX_OK) {
    return status;
  }

  // If the pager is not set up, InitVmos() calls Verify() as its final step. Return here.
  if (!vn->page_watcher_) {
    return status;
  }

  return vn->Verify();
}

BlobCache& Blob::Cache() { return blobfs_->Cache(); }

bool Blob::ShouldCache() const {
  switch (GetState()) {
    // All "Valid", cacheable states, where the blob still exists on storage.
    case kBlobStateReadable:
      return true;
    default:
      return false;
  }
}

void Blob::ActivateLowMemory() {
  // We shouldn't be putting the blob into a low-memory state while it is still mapped.
  ZX_ASSERT(clone_watcher_.object() == ZX_HANDLE_INVALID);
  page_watcher_.reset();
  if (mapping_.vmo()) {
    blobfs_->DetachVmo(vmoid_);
  }
  mapping_.Reset();
}

Blob::~Blob() { ActivateLowMemory(); }

fs::VnodeProtocolSet Blob::GetProtocols() const { return fs::VnodeProtocol::kFile; }

bool Blob::ValidateRights(fs::Rights rights) {
  // To acquire write access to a blob, it must be empty.
  return !rights.write || (GetState() == kBlobStateEmpty);
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
  auto event = blobfs_->Metrics().NewLatencyEvent(fs_metrics::Event::kRead);

  return ReadInternal(data, len, off, out_actual);
}

zx_status_t Blob::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
  TRACE_DURATION("blobfs", "Blob::Write", "len", len, "off", offset);
  auto event = blobfs_->Metrics().NewLatencyEvent(fs_metrics::Event::kWrite);
  return WriteInternal(data, len, out_actual);
}

zx_status_t Blob::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
  auto event = blobfs_->Metrics().NewLatencyEvent(fs_metrics::Event::kAppend);
  zx_status_t status = WriteInternal(data, len, out_actual);
  if (GetState() == kBlobStateDataWrite) {
    ZX_DEBUG_ASSERT(write_info_ != nullptr);
    *out_actual = write_info_->bytes_written;
  } else {
    *out_actual = inode_.blob_size;
  }
  return status;
}

zx_status_t Blob::GetAttributes(fs::VnodeAttributes* a) {
  auto event = blobfs_->Metrics().NewLatencyEvent(fs_metrics::Event::kGetAttr);
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
  auto event = blobfs_->Metrics().NewLatencyEvent(fs_metrics::Event::kTruncate);
  return SpaceAllocate(len);
}

#ifdef __Fuchsia__

constexpr const char kFsName[] = "blobfs";

zx_status_t Blob::QueryFilesystem(::llcpp::fuchsia::io::FilesystemInfo* info) {
  static_assert(fbl::constexpr_strlen(kFsName) + 1 < ::llcpp::fuchsia::io::MAX_FS_NAME_BUFFER,
                "Blobfs name too long");

  *info = {};
  info->block_size = kBlobfsBlockSize;
  info->max_filename_size = digest::kSha256HexLength;
  info->fs_type = VFS_TYPE_BLOBFS;
  info->fs_id = blobfs_->GetFsIdLegacy();
  info->total_bytes = blobfs_->Info().data_block_count * blobfs_->Info().block_size;
  info->used_bytes = blobfs_->Info().alloc_block_count * blobfs_->Info().block_size;
  info->total_nodes = blobfs_->Info().inode_count;
  info->used_nodes = blobfs_->Info().alloc_inode_count;
  strlcpy(reinterpret_cast<char*>(info->name.data()), kFsName,
          ::llcpp::fuchsia::io::MAX_FS_NAME_BUFFER);
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
  return CloneVmo(rights, out_vmo, out_size);
}

#endif

void Blob::Sync(SyncCallback closure) {
  auto event = blobfs_->Metrics().NewLatencyEvent(fs_metrics::Event::kSync);
  if (atomic_load(&syncing_)) {
    blobfs_->Sync(
        [this, evt = std::move(event), cb = std::move(closure)](zx_status_t status) mutable {
          if (status != ZX_OK) {
            cb(status);
            return;
          }

          fs::WriteTxn sync_txn(blobfs_);
          sync_txn.EnqueueFlush();
          status = sync_txn.Transact();
          cb(status);
        });
  } else {
    closure(ZX_OK);
  }
}

void Blob::CompleteSync() {
  atomic_store(&syncing_, false);
  // Drop the write info, since we no longer need it.
  write_info_.reset();
}

fbl::RefPtr<Blob> Blob::CloneWatcherTeardown() {
  if (clone_watcher_.is_pending()) {
    clone_watcher_.Cancel();
    clone_watcher_.set_object(ZX_HANDLE_INVALID);
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
  auto event = blobfs_->Metrics().NewLatencyEvent(fs_metrics::Event::kClose);
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

  if (GetState() == kBlobStateReadable) {
    // A readable blob should only be purged if it has been unlinked.
    ZX_ASSERT(DeletionQueued());
    storage::UnbufferedOperationsBuilder operations;
    fbl::Vector<storage::BufferedOperation> trim_data;
    blobfs_->FreeInode(GetMapIndex(), &operations, &trim_data);

    auto task = fs::wrap_reference(blobfs_->journal()->WriteMetadata(operations.TakeOperations()),
                                   fbl::RefPtr(this))
                    .and_then(blobfs_->journal()->TrimData(std::move(trim_data)));
    blobfs_->journal()->schedule_task(std::move(task));
  }
  ZX_ASSERT(Cache().Evict(fbl::RefPtr(this)) == ZX_OK);
  SetState(kBlobStatePurged);
  return ZX_OK;
}

}  // namespace blobfs
