// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <blobfs/blobfs.h>
#include <blobfs/compression/lz4.h>
#include <blobfs/compression/zstd.h>
#include <blobfs/iterator/allocated-extent-iterator.h>
#include <blobfs/iterator/block-iterator.h>
#include <blobfs/iterator/extent-iterator.h>
#include <blobfs/iterator/node-populator.h>
#include <blobfs/iterator/vector-extent-iterator.h>
#include <blobfs/latency-event.h>
#include <blobfs/writeback.h>
#include <cobalt-client/cpp/timer.h>
#include <digest/digest.h>
#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fs/metrics.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/vfs.h>
#include <lib/sync/completion.h>
#include <zircon/device/vfs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

namespace blobfs {
namespace {

using digest::Digest;
using digest::MerkleTree;

// Blob's vmo names have following pattern
// "blob-1abc8" or "compressedBlob-5c"
constexpr char kBlobVmoNamePrefix[] = "blob";
constexpr char kCompressedBlobVmoNamePrefix[] = "compressedBlob";

void FormatVmoName(const char* prefix,
                   fbl::StringBuffer<ZX_MAX_NAME_LEN>* vmo_name,
                   size_t index) {
    vmo_name->Clear();
    vmo_name->AppendPrintf("%s-%lx", prefix, index);
}

} // namespace

zx_status_t Blob::Verify() const {
    TRACE_DURATION("blobfs", "Blobfs::Verify");
    fs::Ticker ticker(blobfs_->LocalMetrics().Collecting());

    const void* data = inode_.blob_size ? GetData() : nullptr;
    const void* tree = inode_.blob_size ? GetMerkle() : nullptr;
    const uint64_t data_size = inode_.blob_size;
    const uint64_t merkle_size = MerkleTree::GetTreeLength(data_size);
    // TODO(smklein): We could lazily verify more of the VMO if
    // we could fault in pages on-demand.
    //
    // For now, we aggressively verify the entire VMO up front.
    Digest digest(GetKey());
    zx_status_t status =
        MerkleTree::Verify(data, data_size, tree, merkle_size, 0, data_size, digest);
    blobfs_->LocalMetrics().UpdateMerkleVerify(data_size, merkle_size, ticker.End());

    if (status != ZX_OK) {
        char name[Digest::kLength * 2 + 1];
        ZX_ASSERT(digest.ToString(name, sizeof(name)) == ZX_OK);
        FS_TRACE_ERROR("blobfs verify(%s) Failure: %s\n", name, zx_status_get_string(status));
    }

    return status;
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
    zx_status_t status = mapping_.CreateAndMap(vmo_size, vmo_name.c_str());
    if (status != ZX_OK) {
        FS_TRACE_ERROR("Failed to initialize vmo; error: %d\n", status);
        return status;
    }
    if ((status = blobfs_->AttachVmo(mapping_.vmo(), &vmoid_)) != ZX_OK) {
        FS_TRACE_ERROR("Failed to attach VMO to block device; error: %d\n", status);
        return status;
    }

    if ((inode_.header.flags & kBlobFlagLZ4Compressed) != 0) {
        if ((status = InitCompressed(CompressionAlgorithm::LZ4)) != ZX_OK) {
            return status;
        }
    } else if ((inode_.header.flags & kBlobFlagZSTDCompressed) != 0) {
        if ((status = InitCompressed(CompressionAlgorithm::ZSTD)) != ZX_OK) {
            return status;
        }
    } else {
        if ((status = InitUncompressed()) != ZX_OK) {
            return status;
        }
    }
    if ((status = Verify()) != ZX_OK) {
        return status;
    }

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t Blob::InitCompressed(CompressionAlgorithm algorithm) {
    TRACE_DURATION("blobfs", "Blobfs::InitCompressed", "size", inode_.blob_size, "blocks",
                   inode_.block_count);
    fs::Ticker ticker(blobfs_->LocalMetrics().Collecting());
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
    AllocatedExtentIterator extent_iter(blobfs_->GetAllocator(), GetMapIndex());
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

    blobfs_->LocalMetrics().UpdateMerkleDecompress(compressed_blocks * kBlobfsBlockSize,
                                                   inode_.blob_size, read_time, ticker.End());
    return ZX_OK;
}

zx_status_t Blob::InitUncompressed() {
    TRACE_DURATION("blobfs", "Blobfs::InitUncompressed", "size", inode_.blob_size, "blocks",
                   inode_.block_count);
    fs::Ticker ticker(blobfs_->LocalMetrics().Collecting());
    fs::ReadTxn txn(blobfs_);
    AllocatedExtentIterator extent_iter(blobfs_->GetAllocator(), GetMapIndex());
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
    blobfs_->LocalMetrics().UpdateMerkleDiskRead(length * kBlobfsBlockSize, ticker.End());
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
    : CacheNode(digest), blobfs_(bs), flags_(kBlobStateEmpty), syncing_(false),
      clone_watcher_(this) {}

void Blob::BlobCloseHandles() {
    mapping_.Reset();
    readable_event_.reset();
}

zx_status_t Blob::SpaceAllocate(uint64_t size_data) {
    TRACE_DURATION("blobfs", "Blobfs::SpaceAllocate", "size_data", size_data);
    fs::Ticker ticker(blobfs_->LocalMetrics().Collecting());

    if (GetState() != kBlobStateEmpty) {
        return ZX_ERR_BAD_STATE;
    }

    auto write_info = std::make_unique<WritebackInfo>();

    // Initialize the inode with known fields.
    memset(inode_.merkle_root_hash, 0, Digest::kLength);
    inode_.blob_size = size_data;
    inode_.block_count = MerkleTreeBlocks(inode_) + static_cast<uint32_t>(BlobDataBlocks(inode_));

    // Special case for the null blob: We skip the write phase.
    if (inode_.blob_size == 0) {
        zx_status_t status = blobfs_->ReserveNodes(1, &write_info->node_indices);
        if (status != ZX_OK) {
            return status;
        }
        map_index_ = write_info->node_indices[0].index();
        write_info_ = std::move(write_info);

        if ((status = Verify()) != ZX_OK) {
            return status;
        }
        SetState(kBlobStateDataWrite);
        if ((status = WriteMetadata()) != ZX_OK) {
            FS_TRACE_ERROR("Null blob metadata fail: %d\n", status);
            return status;
        }
        return ZX_OK;
    }

    fbl::Vector<ReservedExtent> extents;
    fbl::Vector<ReservedNode> nodes;

    // Reserve space for the blob.
    zx_status_t status = blobfs_->ReserveBlocks(inode_.block_count, &extents);
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
    status = blobfs_->ReserveNodes(node_count, &nodes);
    if (status != ZX_OK) {
        return status;
    }

    fbl::StringBuffer<ZX_MAX_NAME_LEN> vmo_name;
    if (inode_.blob_size >= kCompressionMinBytesSaved) {
        write_info->compressor = BlobCompressor::Create(CompressionAlgorithm::ZSTD,
                                                        inode_.blob_size);
        if (!write_info->compressor) {
            FS_TRACE_ERROR("blobfs: Failed to initialize compressor: %d\n", status);
            return status;
        }
    }

    // Open VMOs, so we can begin writing after allocate succeeds.
    fzl::OwnedVmoMapper mapping;
    FormatVmoName(kBlobVmoNamePrefix, &vmo_name, Ino());
    if ((status = mapping.CreateAndMap(inode_.block_count * kBlobfsBlockSize,
                                       vmo_name.c_str())) != ZX_OK) {
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
    blobfs_->LocalMetrics().UpdateAllocation(size_data, ticker.End());
    return ZX_OK;
}

void* Blob::GetData() const {
    return fs::GetBlock(kBlobfsBlockSize, mapping_.start(), MerkleTreeBlocks(inode_));
}

void* Blob::GetMerkle() const {
    return mapping_.start();
}

zx_status_t Blob::WriteMetadata() {
    TRACE_DURATION("blobfs", "Blobfs::WriteMetadata");
    assert(GetState() == kBlobStateDataWrite);

    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = blobfs_->CreateWork(&wb, this)) != ZX_OK) {
        return status;
    }

    // Update the on-disk hash.
    memcpy(inode_.merkle_root_hash, GetKey(), Digest::kLength);

    // All data has been written to the containing VMO.
    SetState(kBlobStateReadable);
    if (readable_event_.is_valid()) {
        status = readable_event_.signal(0u, ZX_USER_SIGNAL_0);
        if (status != ZX_OK) {
            SetState(kBlobStateError);
            return status;
        }
    }

    atomic_store(&syncing_, true);

    if (inode_.block_count) {
        // We utilize the NodePopulator class to take our reserved blocks and nodes and fill the
        // persistent map with an allocated inode / container.

        // If |on_node| is invoked on a node, it means that node was necessary to represent this
        // blob. Persist the node back to durable storge.
        auto on_node = [this, &wb](const ReservedNode& node) {
            blobfs_->PersistNode(wb.get(), node.index());
        };

        // If |on_extent| is invoked on an extent, it was necessary to represent this blob. Persist
        // the allocation of these blocks back to durable storage.
        //
        // Additionally, because of the compression feature of blobfs, it is possible we reserved
        // more extents than this blob ended up using. Decrement |remaining_blocks| to track if we
        // should exit early.
        size_t remaining_blocks = inode_.block_count;
        auto on_extent = [this, &wb, &remaining_blocks](ReservedExtent& extent) {
            ZX_DEBUG_ASSERT(remaining_blocks > 0);
            if (remaining_blocks >= extent.extent().Length()) {
                // Consume the entire extent.
                remaining_blocks -= extent.extent().Length();
            } else {
                // Consume only part of the extent; we're done iterating.
                extent.SplitAt(static_cast<BlockCountType>(remaining_blocks));
                remaining_blocks = 0;
            }
            blobfs_->PersistBlocks(wb.get(), extent);
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
        const uint32_t non_allocation_flags = kBlobFlagZSTDCompressed | kBlobFlagLZ4Compressed;
        mapped_inode->header.flags |= (inode_.header.flags & non_allocation_flags);
    } else {
        // Special case: Empty node.
        ZX_DEBUG_ASSERT(write_info_->node_indices.size() == 1);
        const ReservedNode& node = write_info_->node_indices[0];
        blobfs_->GetAllocator()->MarkInodeAllocated(node);
        blobfs_->PersistNode(wb.get(), node.index());
    }

    wb->SetSyncCallback([blob = fbl::WrapRefPtr(this)](zx_status_t status) {
        if (status == ZX_OK) {
            blob->CompleteSync();
        }
    });
    if ((status = blobfs_->EnqueueWork(std::move(wb), EnqueueType::kJournal)) != ZX_OK) {
        return status;
    }

    // Drop the write info, since we no longer need it.
    write_info_.reset();
    return status;
}

zx_status_t Blob::WriteInternal(const void* data, size_t len, size_t* actual) {
    TRACE_DURATION("blobfs", "Blobfs::WriteInternal", "data", data, "len", len);

    *actual = 0;
    if (len == 0) {
        return ZX_OK;
    }

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

        // Only write data to disk once we've buffered the file into memory.
        // This gives us a chance to try compressing the blob before we write it back.
        fbl::unique_ptr<WritebackWork> wb;
        if ((status = blobfs_->CreateWork(&wb, this)) != ZX_OK) {
            return status;
        }

        // In case the operation fails, forcibly reset the WritebackWork
        // to avoid asserting that no write requests exist on destruction.
        auto set_error = fbl::MakeAutoCall([&]() {
            if (wb != nullptr) {
                wb->MarkCompleted(ZX_ERR_BAD_STATE);
            }

            SetState(kBlobStateError);
        });

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

        // TODO(smklein): As an optimization, use the CreateInit/Update/Final
        // methods to create the merkle tree as we write data, rather than
        // waiting until the data is fully downloaded to create the tree.
        size_t merkle_size = MerkleTree::GetTreeLength(inode_.blob_size);
        fs::Duration generation_time;
        if (merkle_size > 0) {
            Digest digest;
            void* merkle_data = GetMerkle();
            const void* blob_data = GetData();
            // Tracking generation time.
            fs::Ticker ticker(blobfs_->LocalMetrics().Collecting());

            if ((status = MerkleTree::Create(blob_data, inode_.blob_size, merkle_data, merkle_size,
                                             &digest)) != ZX_OK) {
                return status;
            } else if (digest != GetKey()) {
                // Downloaded blob did not match provided digest.
                return ZX_ERR_IO_DATA_INTEGRITY;
            }

            status = StreamBlocks(&block_iter, merkle_blocks,
                                  [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
                                      return EnqueuePaginated(
                                          &wb, blobfs_, this, mapping_.vmo(), vmo_offset,
                                          dev_offset + blobfs_->DataStart(), length);
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
                                      return EnqueuePaginated(
                                          &wb, blobfs_, this, write_info_->compressor->Vmo(),
                                          vmo_offset - merkle_blocks,
                                          dev_offset + blobfs_->DataStart(), length);
                                  });

            if (status != ZX_OK) {
                return status;
            }
            blocks += MerkleTreeBlocks(inode_);
            // By compressing, we used less blocks than we originally reserved.
            ZX_DEBUG_ASSERT(inode_.block_count > blocks);

            inode_.block_count = blocks;
            inode_.header.flags |= kBlobFlagZSTDCompressed;
        } else {
            uint64_t blocks64 =
                fbl::round_up(inode_.blob_size, kBlobfsBlockSize) / kBlobfsBlockSize;
            ZX_DEBUG_ASSERT(blocks64 <= std::numeric_limits<uint32_t>::max());
            uint32_t blocks = static_cast<uint32_t>(blocks64);
            status = StreamBlocks(&block_iter, blocks,
                                  [&](uint64_t vmo_offset, uint64_t dev_offset, uint32_t length) {
                                      return EnqueuePaginated(
                                          &wb, blobfs_, this, mapping_.vmo(), vmo_offset,
                                          dev_offset + blobfs_->DataStart(), length);
                                  });
            if (status != ZX_OK) {
                return status;
            }
        }

        // Enqueue the blob's final data work. Metadata must be enqueued separately.
        if ((status = blobfs_->EnqueueWork(std::move(wb), EnqueueType::kData)) != ZX_OK) {
            return status;
        }

        // No more data to write. Flush to disk.
        fs::Ticker ticker(blobfs_->LocalMetrics().Collecting()); // Tracking enqueue time.
        if ((status = WriteMetadata()) != ZX_OK) {
            return status;
        }

        blobfs_->LocalMetrics().UpdateClientWrite(to_write, merkle_size, ticker.End(),
                                                  generation_time);
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

zx_status_t Blob::GetReadableEvent(zx_handle_t* out) {
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

    *out = out_event.release();
    return ZX_OK;
}

zx_status_t Blob::CloneVmo(zx_rights_t rights, zx_handle_t* out_vmo, size_t* out_size) {
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

    // TODO(smklein): Only clone / verify the part of the vmo that
    // was requested.
    const size_t merkle_bytes = MerkleTreeBlocks(inode_) * kBlobfsBlockSize;
    zx::vmo clone;
    if ((status = mapping_.vmo().create_child(ZX_VMO_CHILD_COPY_ON_WRITE,
                                              merkle_bytes, inode_.blob_size,
                                              &clone)) != ZX_OK) {
        return status;
    }

    // TODO(mdempsky): Push elsewhere.
    if ((status = clone.replace_as_executable(zx::handle(), &clone)) != ZX_OK) {
        return status;
    }

    if ((status = clone.replace(rights, &clone)) != ZX_OK) {
        return status;
    }
    *out_vmo = clone.release();
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

void Blob::HandleNoClones(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                               zx_status_t status, const zx_packet_signal_t* signal) {
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
    // Since InitVmos calls Verify as its final step, we can just return its result here.
    return vn->InitVmos();
}

BlobCache& Blob::Cache() {
    return blobfs_->Cache();
}

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
    if (mapping_.vmo()) {
        blobfs_->DetachVmo(vmoid_);
    }
    mapping_.Reset();
}

Blob::~Blob() {
    ActivateLowMemory();
}

zx_status_t Blob::ValidateFlags(uint32_t flags) {
    if (flags & ZX_FS_FLAG_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }

    if (flags & ZX_FS_RIGHT_WRITABLE) {
        if (GetState() != kBlobStateEmpty) {
            return ZX_ERR_ACCESS_DENIED;
        }
    }
    return ZX_OK;
}

zx_status_t Blob::GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) {
    info->tag = fuchsia_io_NodeInfoTag_file;
    return GetReadableEvent(&info->file.event);
}

zx_status_t Blob::Read(void* data, size_t len, size_t off, size_t* out_actual) {
    TRACE_DURATION("blobfs", "Blob::Read", "len", len, "off", off);
    LatencyEvent event(&blobfs_->GetMutableVnodeMetrics()->read, blobfs_->CollectingMetrics());

    return ReadInternal(data, len, off, out_actual);
}

zx_status_t Blob::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
    TRACE_DURATION("blobfs", "Blob::Write", "len", len, "off", offset);
    LatencyEvent event(&blobfs_->GetMutableVnodeMetrics()->write, blobfs_->CollectingMetrics());
    return WriteInternal(data, len, out_actual);
}

zx_status_t Blob::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
    LatencyEvent event(&blobfs_->GetMutableVnodeMetrics()->append, blobfs_->CollectingMetrics());
    zx_status_t status = WriteInternal(data, len, out_actual);
    if (GetState() == kBlobStateDataWrite) {
        ZX_DEBUG_ASSERT(write_info_ != nullptr);
        *out_actual = write_info_->bytes_written;
    } else {
        *out_actual = inode_.blob_size;
    }
    return status;
}

zx_status_t Blob::Getattr(vnattr_t* a) {
    LatencyEvent event(&blobfs_->GetMutableVnodeMetrics()->get_attr, blobfs_->CollectingMetrics());
    memset(a, 0, sizeof(vnattr_t));
    a->mode = V_TYPE_FILE | V_IRUSR;
    a->inode = Ino();
    a->size = SizeData();
    a->blksize = kBlobfsBlockSize;
    a->blkcount = inode_.block_count * (kBlobfsBlockSize / VNATTR_BLKSIZE);
    a->nlink = 1;
    a->create_time = 0;
    a->modify_time = 0;
    return ZX_OK;
}

zx_status_t Blob::Truncate(size_t len) {
    TRACE_DURATION("blobfs", "Blob::Truncate", "len", len);
    LatencyEvent event(&blobfs_->GetMutableVnodeMetrics()->truncate, blobfs_->CollectingMetrics());
    return SpaceAllocate(len);
}

#ifdef __Fuchsia__

constexpr const char kFsName[] = "blobfs";

zx_status_t Blob::QueryFilesystem(fuchsia_io_FilesystemInfo* info) {
    static_assert(fbl::constexpr_strlen(kFsName) + 1 < fuchsia_io_MAX_FS_NAME_BUFFER,
                  "Blobfs name too long");

    memset(info, 0, sizeof(*info));
    info->block_size = kBlobfsBlockSize;
    info->max_filename_size = Digest::kLength * 2;
    info->fs_type = VFS_TYPE_BLOBFS;
    info->fs_id = blobfs_->GetFsId();
    info->total_bytes = blobfs_->Info().data_block_count * blobfs_->Info().block_size;
    info->used_bytes = blobfs_->Info().alloc_block_count * blobfs_->Info().block_size;
    info->total_nodes = blobfs_->Info().inode_count;
    info->used_nodes = blobfs_->Info().alloc_inode_count;
    strlcpy(reinterpret_cast<char*>(info->name), kFsName, fuchsia_io_MAX_FS_NAME_BUFFER);
    return ZX_OK;
}

zx_status_t Blob::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
    auto device = blobfs_->BlockDevice();
    if (buffer_len == 0) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    zx_status_t call_status;
    zx_status_t status = fuchsia_device_ControllerGetTopologicalPath(device->get(), &call_status,
                                                                     out_name, buffer_len - 1,
                                                                     out_len);
    if (status == ZX_OK) {
        status = call_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    // Ensure null-terminated
    out_name[*out_len] = 0;
    // Account for the null byte in the length, since callers expect it.
    (*out_len)++;
    return ZX_OK;
}
#endif

zx_status_t Blob::GetVmo(int flags, zx_handle_t* out_vmo, size_t* out_size) {
    TRACE_DURATION("blobfs", "Blob::GetVmo", "flags", flags);

    if (flags & fuchsia_io_VMO_FLAG_WRITE) {
        return ZX_ERR_NOT_SUPPORTED;
    } else if (flags & fuchsia_io_VMO_FLAG_EXACT) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Let clients map and set the names of their VMOs.
    zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHTS_PROPERTY;
    // We can ignore fuchsia_io_VMO_FLAG_PRIVATE, since private / shared access
    // to the underlying VMO can both be satisfied with a clone due to
    // the immutability of blobfs blobs.
    rights |= (flags & fuchsia_io_VMO_FLAG_READ) ? ZX_RIGHT_READ : 0;
    rights |= (flags & fuchsia_io_VMO_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
    return CloneVmo(rights, out_vmo, out_size);
}

void Blob::Sync(SyncCallback closure) {
    LatencyEvent event(&blobfs_->GetMutableVnodeMetrics()->sync, blobfs_->CollectingMetrics());
    if (atomic_load(&syncing_)) {
        blobfs_->Sync([this, evt = std::move(event), cb = std::move(closure)](zx_status_t status) {
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

bool Blob::IsDirectory() const {
    return false;
}

void Blob::CompleteSync() {
    atomic_store(&syncing_, false);
}

fbl::RefPtr<Blob> Blob::CloneWatcherTeardown() {
    if (clone_watcher_.is_pending()) {
        clone_watcher_.Cancel();
        clone_watcher_.set_object(ZX_HANDLE_INVALID);
        return std::move(clone_ref_);
    }
    return nullptr;
}

zx_status_t Blob::Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) {
    fd_count_++;
    return ZX_OK;
}

zx_status_t Blob::Close() {
    LatencyEvent event(&blobfs_->GetMutableVnodeMetrics()->close, blobfs_->CollectingMetrics());
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
        fbl::unique_ptr<WritebackWork> wb;
        zx_status_t status = blobfs_->CreateWork(&wb, this);
        if (status != ZX_OK) {
            return status;
        }

        blobfs_->FreeInode(wb.get(), GetMapIndex());
        status = blobfs_->EnqueueWork(std::move(wb), EnqueueType::kJournal);
        if (status != ZX_OK) {
            return status;
        }
    }
    ZX_ASSERT(Cache().Evict(fbl::WrapRefPtr(this)) == ZX_OK);
    SetState(kBlobStatePurged);
    return ZX_OK;
}

} // namespace blobfs
