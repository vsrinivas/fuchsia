// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/host.h"

#include <fcntl.h>
#include <inttypes.h>
#include <lib/cksum.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/status.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <fs-host/common.h>
#include <safemath/checked_math.h>
#include <safemath/safe_conversions.h>

#include "src/lib/digest/digest.h"
#include "src/lib/digest/merkle-tree.h"
#include "src/lib/digest/node-digest.h"
#include "src/lib/storage/vfs/cpp/journal/initializer.h"
#include "src/lib/storage/vfs/cpp/transaction/transaction_handler.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression/compressor.h"
#include "src/storage/blobfs/compression/decompressor.h"
#include "src/storage/blobfs/compression_settings.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/fsck_host.h"
#include "src/storage/blobfs/iterator/allocated_extent_iterator.h"
#include "src/storage/blobfs/iterator/block_iterator.h"

using digest::Digest;
using digest::MerkleTreeCreator;
using digest::MerkleTreeVerifier;

constexpr uint32_t kExtentCount = 5;

namespace blobfs {
namespace {

// TODO(markdittmer): Abstract choice of host compressor, decompressor and metadata flag to support
// choosing from multiple strategies. This has already been done in non-host code but host tools do
// not use |BlobCompressor| the same way.
using HostCompressor = ChunkedCompressor;
using HostDecompressor = ChunkedDecompressor;

constexpr CompressionSettings kCompressionSettings = {
    .compression_algorithm = CompressionAlgorithm::kChunked,
};

zx::status<> ReadBlocksWithOffset(int fd, uint64_t start_block, uint64_t block_count,
                                  off_t file_offset, void* data) {
  off_t off = safemath::checked_cast<off_t>(
      safemath::CheckAdd(file_offset,
                         safemath::CheckMul(start_block, kBlobfsBlockSize).ValueOrDie())
          .ValueOrDie());
  size_t size = safemath::CheckMul(block_count, kBlobfsBlockSize).ValueOrDie();
  auto udata = static_cast<uint8_t*>(data);
  while (size > 0) {
    ssize_t ret = pread(fd, udata, size, off);
    if (ret <= 0) {
      perror("failed read");
      FX_LOGS(ERROR) << "cannot read block " << start_block << " size:" << size << " off:" << off;
      return zx::error(ZX_ERR_IO);
    }
    size -= ret;
    off += ret;
    udata += ret;
  }
  return zx::ok();
}

zx::status<> ReadBlockWithOffset(int fd, uint64_t block_number, off_t file_offset, void* data) {
  return ReadBlocksWithOffset(fd, block_number, /*block_count=*/1, file_offset, data);
}

zx::status<> WriteBlocksWithOffset(int fd, uint64_t start_block, uint64_t block_count,
                                   off_t file_offset, const void* data) {
  off_t off = safemath::checked_cast<off_t>(
      safemath::CheckAdd(file_offset,
                         safemath::CheckMul(start_block, kBlobfsBlockSize).ValueOrDie())
          .ValueOrDie());
  size_t size = safemath::CheckMul(block_count, kBlobfsBlockSize).ValueOrDie();
  auto udata = static_cast<const uint8_t*>(data);
  while (size > 0) {
    ssize_t ret = pwrite(fd, udata, size, off);
    if (ret < 0) {
      perror("failed write");
      FX_LOGS(ERROR) << "cannot write block " << start_block << " size:" << size << " off:" << off;
      return zx::error(ZX_ERR_IO);
    }
    size -= ret;
    off += ret;
    udata += ret;
  }
  return zx::ok();
}

zx::status<> WriteBlocks(int fd, uint64_t start_block, uint64_t block_count, const void* data) {
  return WriteBlocksWithOffset(fd, start_block, block_count, /*file_offset=*/0, data);
}

zx::status<> WriteBlock(int fd, uint64_t block_number, const void* data) {
  return WriteBlocks(fd, block_number, /*block_count=*/1, data);
}

struct MerkleTreeInfo {
  static zx::status<MerkleTreeInfo> Create(fbl::Span<const uint8_t> data,
                                           BlobLayoutFormat blob_layout_format) {
    MerkleTreeCreator mtc;
    mtc.SetUseCompactFormat(blob_layout_format == BlobLayoutFormat::kCompactMerkleTreeAtEnd);
    if (zx_status_t status = mtc.SetDataLength(data.size()); status != ZX_OK) {
      return zx::error(status);
    }

    std::vector<uint8_t> merkle_tree(mtc.GetTreeLength(), 0);
    uint8_t root[digest::kSha256Length];
    if (zx_status_t status =
            mtc.SetTree(merkle_tree.data(), merkle_tree.size(), root, digest::kSha256Length);
        status != ZX_OK) {
      return zx::error(status);
    }
    if (zx_status_t status = mtc.Append(data.data(), data.size()); status != ZX_OK) {
      return zx::error(status);
    }
    MerkleTreeInfo mti;
    mti.digest = root;
    mti.merkle_tree = std::move(merkle_tree);
    return zx::ok(std::move(mti));
  }

  Digest digest;
  std::vector<uint8_t> merkle_tree;
};

zx::status<std::vector<uint8_t>> CompressData(fbl::Span<const uint8_t> data) {
  size_t max = HostCompressor::BufferMax(data.size());
  std::vector<uint8_t> compressed_data(max, 0);

  std::unique_ptr<HostCompressor> compressor;
  size_t output_limit;
  if (zx_status_t status =
          HostCompressor::Create(kCompressionSettings, data.size(), &output_limit, &compressor);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize blobfs compressor: " << status;
    return zx::error(status);
  }

  if (zx_status_t status = compressor->SetOutput(compressed_data.data(), max); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize blobfs compressor: " << status;
    return zx::error(status);
  }

  if (zx_status_t status = compressor->Update(data.data(), data.size()); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to update blobfs compressor: " << status;
    return zx::error(status);
  }

  if (zx_status_t status = compressor->End(); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to complete blobfs compressor: " << status;
    return zx::error(status);
  }

  compressed_data.resize(compressor->Size());
  return zx::ok(std::move(compressed_data));
}

// Returns ZX_OK and copies blobfs info_block_t, which is a block worth of data containing
// superblock, into |out_info_block| if the block read from fd belongs to blobfs.
zx_status_t blobfs_load_info_block(const fbl::unique_fd& fd, info_block_t* out_info_block,
                                   off_t start = 0, std::optional<off_t> end = std::nullopt) {
  info_block_t info_block;

  if (ReadBlockWithOffset(fd.get(), /*block_number=*/0, start,
                          reinterpret_cast<void*>(info_block.block))
          .is_error()) {
    return ZX_ERR_IO;
  }
  uint64_t block_count;
  if (zx_status_t status = GetBlockCount(fd.get(), &block_count); status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot find end of underlying device";
    return status;
  }

  if (end &&
      ((block_count * kBlobfsBlockSize) < safemath::checked_cast<uint64_t>(end.value() - start))) {
    FX_LOGS(ERROR) << "Invalid file size " << safemath::checked_cast<uint64_t>(end.value() - start);
    return ZX_ERR_BAD_STATE;
  }
  if (zx_status_t status = CheckSuperblock(&info_block.info, block_count); status != ZX_OK) {
    FX_LOGS(ERROR) << "Info check failed " << status;
    return status;
  }

  memcpy(out_info_block, &info_block, sizeof(*out_info_block));

  return ZX_OK;
}

zx_status_t get_superblock(const fbl::unique_fd& fd, off_t start, std::optional<off_t> end,
                           Superblock* info) {
  info_block_t info_block;
  zx_status_t status;

  if ((status = blobfs_load_info_block(fd, &info_block, start, end)) != ZX_OK) {
    FX_LOGS(ERROR) << "Load of info block failed " << status;
    return status;
  }

  memcpy(info, &info_block.info, sizeof(info_block.info));
  return ZX_OK;
}

}  // namespace

zx::status<FileMapping> FileMapping::Create(int fd) {
  struct stat s;
  if (fstat(fd, &s) < 0) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  if (s.st_size == 0) {
    // Empty files can't be mapped.
    return zx::ok(FileMapping(nullptr, 0));
  }

  void* data = mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (data == MAP_FAILED) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  return zx::ok(FileMapping(data, s.st_size));
}

FileMapping::FileMapping(FileMapping&& other) noexcept
    : data_(other.data_), length_(other.length_) {
  other.data_ = nullptr;
  other.length_ = 0;
}

FileMapping& FileMapping::operator=(FileMapping&& other) noexcept {
  data_ = other.data_;
  length_ = other.length_;
  other.data_ = nullptr;
  other.length_ = 0;
  return *this;
}

FileMapping::~FileMapping() {
  if (data_ != nullptr) {
    munmap(data_, length_);
  }
}

zx::status<BlobInfo> BlobInfo::CreateCompressed(int fd, BlobLayoutFormat blob_layout_format,
                                                std::optional<std::filesystem::path> file_path) {
  zx::status<BlobInfo> blob_info = CreateUncompressed(fd, blob_layout_format, std::move(file_path));
  if (blob_info.is_error()) {
    return blob_info;
  }

  fbl::Span<const uint8_t> data = blob_info->GetData();
  if (data.size() <= kCompressionSizeThresholdBytes) {
    // The blob is already small and compressing wouldn't save any space, leave the blob
    // uncompressed.
    return blob_info;
  }

  zx::status<std::vector<uint8_t>> compressed_data = CompressData(data);
  if (compressed_data.is_error()) {
    return compressed_data.take_error();
  }

  zx::status<std::unique_ptr<BlobLayout>> compressed_blob_layout = BlobLayout::CreateFromSizes(
      blob_layout_format, data.size(), compressed_data->size(), kBlobfsBlockSize);
  if (compressed_blob_layout.is_error()) {
    return compressed_blob_layout.take_error();
  }

  if (compressed_blob_layout->TotalBlockCount() >= blob_info->GetBlobLayout().TotalBlockCount()) {
    // Compressing the blob didn't save any blocks, leave the blob uncompressed.
    return blob_info;
  }
  // Replace the uncompressed data with the compressed data.
  blob_info->blob_layout_ = std::move(compressed_blob_layout).value();
  blob_info->blob_data_ = std::move(compressed_data).value();

  return blob_info;
}

zx::status<BlobInfo> BlobInfo::CreateUncompressed(int fd, BlobLayoutFormat blob_layout_format,
                                                  std::optional<std::filesystem::path> file_path) {
  BlobInfo blob_info;
  blob_info.src_file_path_ = std::move(file_path);

  zx::status<FileMapping> file_mapping = FileMapping::Create(fd);
  if (file_mapping.is_error()) {
    return file_mapping.take_error();
  }

  fbl::Span<const uint8_t> data = file_mapping->data();
  zx::status<MerkleTreeInfo> merkle_tree_info = MerkleTreeInfo::Create(data, blob_layout_format);
  if (merkle_tree_info.is_error()) {
    return merkle_tree_info.take_error();
  }
  blob_info.merkle_tree_ = std::move(merkle_tree_info->merkle_tree);
  blob_info.digest_ = std::move(merkle_tree_info->digest);

  auto blob_layout =
      BlobLayout::CreateFromSizes(blob_layout_format, data.size(), data.size(), kBlobfsBlockSize);
  if (blob_layout.is_error()) {
    return blob_layout.take_error();
  }
  blob_info.blob_layout_ = std::move(blob_layout).value();

  blob_info.blob_data_.emplace<FileMapping>(std::move(file_mapping).value());

  return zx::ok(std::move(blob_info));
}

zx_status_t ReadBlock(int fd, uint64_t block_number, void* data) {
  return ReadBlockWithOffset(fd, block_number, /*file_offset=*/0, data).status_value();
}

zx_status_t GetBlockCount(int fd, uint64_t* out) {
  struct stat s;
  if (fstat(fd, &s) < 0) {
    return ZX_ERR_BAD_STATE;
  }
  *out = s.st_size / kBlobfsBlockSize;
  return ZX_OK;
}

int Mkfs(int fd, uint64_t block_count, const FilesystemOptions& options) {
  Superblock info;
  InitializeSuperblock(block_count, options, &info);
  zx_status_t status = CheckSuperblock(&info, block_count);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize superblock: " << status;
    return -1;
  }
  uint64_t block_bitmap_blocks = BlockMapBlocks(info);
  uint64_t node_map_blocks = NodeMapBlocks(info);

  RawBitmap block_bitmap;
  if (block_bitmap.Reset(block_bitmap_blocks * kBlobfsBlockBits)) {
    FX_LOGS(ERROR) << "Couldn't allocate blobfs block map";
    return -1;
  }
  if (block_bitmap.Shrink(info.data_block_count)) {
    FX_LOGS(ERROR) << "Couldn't shrink blobfs block map";
    return -1;
  }

  // Reserve first |kStartBlockMinimum| data blocks
  block_bitmap.Set(0, kStartBlockMinimum);

  // All in-memory structures have been created successfully. Dump everything to disk.
  // Initialize on-disk journal.
  fs::WriteBlocksFn write_blocks_fn = [fd, &info](fbl::Span<const uint8_t> buffer,
                                                  uint64_t block_offset, uint64_t block_count) {
    ZX_ASSERT((block_offset + block_count) <= JournalBlocks(info));
    ZX_ASSERT(buffer.size() >= (block_count * kBlobfsBlockSize));
    return WriteBlocks(fd, JournalStartBlock(info) + block_offset, block_count, buffer.data())
        .status_value();
  };
  status = fs::MakeJournal(JournalBlocks(info), write_blocks_fn);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to write journal block";
    return -1;
  }

  // Write the root block to disk.
  static_assert(kBlobfsBlockSize == sizeof(info));
  if (WriteBlock(fd, 0, &info).is_error()) {
    FX_LOGS(ERROR) << "Failed to write Superblock";
    return -1;
  }

  // Write allocation bitmap to disk.
  if (WriteBlocks(fd, BlockMapStartBlock(info), block_bitmap_blocks,
                  block_bitmap.StorageUnsafe()->GetData())
          .is_error()) {
    FX_LOGS(ERROR) << "Failed to write blockmap block " << block_bitmap_blocks;
    return -1;
  }

  // Write node map to disk.
  size_t map_length = node_map_blocks * kBlobfsBlockSize;
  void* blocks = mmap(nullptr, map_length, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (blocks == MAP_FAILED) {
    FX_LOGS(ERROR) << "failed to map zeroes for inode map of size " << map_length;
    return -1;
  }
  if (WriteBlocks(fd, NodeMapStartBlock(info), node_map_blocks, blocks).is_error()) {
    FX_LOGS(ERROR) << "failed writing inode map";
    munmap(blocks, map_length);
    return -1;
  }
  if (munmap(blocks, map_length) != 0) {
    FX_LOGS(ERROR) << "failed unmap inode map";
    return -1;
  }

  FX_LOGS(DEBUG) << "mkfs success";
  return 0;
}

zx_status_t UsedDataSize(const fbl::unique_fd& fd, uint64_t* out_size, off_t start,
                         std::optional<off_t> end) {
  Superblock info;
  zx_status_t status;

  if ((status = get_superblock(fd, start, end, &info)) != ZX_OK) {
    return status;
  }

  *out_size = info.alloc_block_count * info.block_size;
  return ZX_OK;
}

zx_status_t UsedInodes(const fbl::unique_fd& fd, uint64_t* out_inodes, off_t start,
                       std::optional<off_t> end) {
  Superblock info;
  zx_status_t status;

  if ((status = get_superblock(fd, start, end, &info)) != ZX_OK) {
    return status;
  }

  *out_inodes = info.alloc_inode_count;
  return ZX_OK;
}

zx_status_t UsedSize(const fbl::unique_fd& fd, uint64_t* out_size, off_t start,
                     std::optional<off_t> end) {
  Superblock info;
  zx_status_t status;

  if ((status = get_superblock(fd, start, end, &info)) != ZX_OK) {
    return status;
  }

  *out_size = (TotalNonDataBlocks(info) + info.alloc_block_count) * info.block_size;
  return ZX_OK;
}

zx_status_t blobfs_create(std::unique_ptr<Blobfs>* out, fbl::unique_fd fd) {
  info_block_t info_block;
  zx_status_t status;

  if ((status = blobfs_load_info_block(fd, &info_block)) != ZX_OK) {
    return status;
  }

  fbl::Array<size_t> extent_lengths(new size_t[kExtentCount], kExtentCount);

  if (info_block.info.flags & kBlobFlagFVM) {
    // The image is assumed to be a sparse file containing an FVM-formatted blobfs image with the
    // various metadata regions at their correct offsets. We just consider the "length" of each
    // extent to be the maximum possible length (i.e.  the number of blocks up to the offset of the
    // next region).
    extent_lengths[0] = BlockMapStartBlock(info_block.info) * kBlobfsBlockSize;
    extent_lengths[1] = (NodeMapStartBlock(info_block.info) - BlockMapStartBlock(info_block.info)) *
                        kBlobfsBlockSize;
    extent_lengths[2] = (JournalStartBlock(info_block.info) - NodeMapStartBlock(info_block.info)) *
                        kBlobfsBlockSize;
    extent_lengths[3] =
        (DataStartBlock(info_block.info) - JournalStartBlock(info_block.info)) * kBlobfsBlockSize;
    extent_lengths[4] = DataBlocks(info_block.info) * kBlobfsBlockSize;
  } else {
    extent_lengths[0] = BlockMapStartBlock(info_block.info) * kBlobfsBlockSize;
    extent_lengths[1] = BlockMapBlocks(info_block.info) * kBlobfsBlockSize;
    extent_lengths[2] = NodeMapBlocks(info_block.info) * kBlobfsBlockSize;
    extent_lengths[3] = JournalBlocks(info_block.info) * kBlobfsBlockSize;
    extent_lengths[4] = DataBlocks(info_block.info) * kBlobfsBlockSize;
  }

  auto blobfs = Blobfs::Create(std::move(fd), 0, info_block, extent_lengths);
  if (blobfs.is_error()) {
    FX_LOGS(ERROR) << "mount failed; could not create blobfs";
    return blobfs.status_value();
  }

  *out = std::move(blobfs).value();
  return ZX_OK;
}

zx_status_t blobfs_create_sparse(std::unique_ptr<Blobfs>* out, fbl::unique_fd fd, off_t start,
                                 off_t end, const std::vector<size_t>& extent_vector) {
  if (start >= end) {
    FX_LOGS(ERROR) << "Insufficient space allocated";
    return ZX_ERR_INVALID_ARGS;
  }
  if (extent_vector.size() != kExtentCount) {
    FX_LOGS(ERROR) << "Incorrect number of extents";
    return ZX_ERR_INVALID_ARGS;
  }

  info_block_t info_block;
  zx_status_t status;

  if ((status = blobfs_load_info_block(fd, &info_block, start, end)) != ZX_OK) {
    return status;
  }

  fbl::Array<size_t> extent_lengths(new size_t[kExtentCount], kExtentCount);

  extent_lengths[0] = extent_vector[0];
  extent_lengths[1] = extent_vector[1];
  extent_lengths[2] = extent_vector[2];
  extent_lengths[3] = extent_vector[3];
  extent_lengths[4] = extent_vector[4];

  auto blobfs = Blobfs::Create(std::move(fd), start, info_block, extent_lengths);
  if (blobfs.is_error()) {
    FX_LOGS(ERROR) << "mount failed; could not create blobfs";
    return blobfs.status_value();
  }

  *out = std::move(blobfs).value();
  return ZX_OK;
}

zx_status_t blobfs_fsck(fbl::unique_fd fd, off_t start, off_t end,
                        const std::vector<size_t>& extent_lengths) {
  std::unique_ptr<Blobfs> blob;
  zx_status_t status;
  if ((status = blobfs_create_sparse(&blob, std::move(fd), start, end, extent_lengths)) != ZX_OK) {
    return status;
  }
  if ((status = Fsck(blob.get())) != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

Blobfs::Blobfs(fbl::unique_fd fd, off_t offset, const info_block_t& info_block,
               const fbl::Array<size_t>& extent_lengths)
    : blockfd_(std::move(fd)), offset_(offset) {
  ZX_ASSERT(extent_lengths.size() == kExtentCount);
  memcpy(&info_block_, info_block.block, kBlobfsBlockSize);

  block_map_start_block_ = extent_lengths[0] / kBlobfsBlockSize;
  block_map_block_count_ = extent_lengths[1] / kBlobfsBlockSize;
  node_map_start_block_ = block_map_start_block_ + block_map_block_count_;
  node_map_block_count_ = extent_lengths[2] / kBlobfsBlockSize;
  journal_start_block_ = node_map_start_block_ + node_map_block_count_;
  journal_block_count_ = extent_lengths[3] / kBlobfsBlockSize;
  data_start_block_ = journal_start_block_ + journal_block_count_;
  data_block_count_ = extent_lengths[4] / kBlobfsBlockSize;
}

zx::status<std::unique_ptr<Blobfs>> Blobfs::Create(fbl::unique_fd blockfd_, off_t offset,
                                                   const info_block_t& info_block,
                                                   const fbl::Array<size_t>& extent_lengths) {
  if (zx_status_t status = CheckSuperblock(&info_block.info, TotalBlocks(info_block.info));
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Check info failure";
    return zx::error(status);
  }

  ZX_ASSERT(extent_lengths.size() == kExtentCount);

  for (unsigned i = 0; i < 3; i++) {
    if (extent_lengths[i] % kBlobfsBlockSize) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }

  auto fs =
      std::unique_ptr<Blobfs>(new Blobfs(std::move(blockfd_), offset, info_block, extent_lengths));

  if (zx::status<> status = fs->LoadBitmap(); status.is_error()) {
    FX_LOGS(ERROR) << "Failed to load bitmaps";
    return status.take_error();
  }
  if (zx::status<> status = fs->LoadNodeMap(); status.is_error()) {
    FX_LOGS(ERROR) << "Failed to load node map";
    return status.take_error();
  }

  return zx::ok(std::move(fs));
}

zx::status<> Blobfs::LoadBitmap() {
  if (zx_status_t status = block_map_.Reset(block_map_block_count_ * kBlobfsBlockBits);
      status != ZX_OK) {
    return zx::error(status);
  }
  if (zx_status_t status = block_map_.Shrink(info_.data_block_count); status != ZX_OK) {
    return zx::error(status);
  }
  return ReadBlocks(block_map_start_block_, block_map_block_count_,
                    block_map_.StorageUnsafe()->GetData());
}

zx::status<> Blobfs::LoadNodeMap() {
  const size_t nodes_to_load = fbl::round_up(Info().inode_count, kBlobfsInodesPerBlock);
  nodes_ = std::make_unique<Inode[]>(nodes_to_load);
  return ReadBlocks(node_map_start_block_, NodeMapBlocks(info_), nodes_.get());
}

zx::status<std::unique_ptr<Blobfs::InodeBlock>> Blobfs::NewBlob(const Digest& digest) {
  std::optional<size_t> free_inode;

  for (size_t i = 0; i < info_.inode_count; ++i) {
    const Inode& inode = nodes_[i];

    if (inode.header.IsAllocated() && !inode.header.IsExtentContainer()) {
      if (digest == inode.merkle_root_hash) {
        FX_LOGS(ERROR) << "Blob already exists " << digest.ToString();
        return zx::error(ZX_ERR_ALREADY_EXISTS);
      }
    } else if (!free_inode.has_value()) {
      // If |ino| has not already been set to a valid value, set it to the first free value we find.
      // We still check all the remaining inodes to avoid adding a duplicate blob.
      free_inode = i;
    }
  }

  if (!free_inode.has_value()) {
    FX_LOGS(ERROR) << "No inode resources left. All " << info_.inode_count << " inodes are in use.";
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  size_t bno = (*free_inode / kBlobfsInodesPerBlock) + NodeMapStartBlock(info_);
  auto ino_block = std::make_unique<InodeBlock>(bno, &nodes_[*free_inode], digest);

  info_.alloc_inode_count++;
  return zx::ok(std::move(ino_block));
}

zx::status<> Blobfs::AddBlob(const BlobInfo& blob_info) {
  const BlobLayout& blob_layout = blob_info.GetBlobLayout();
  if (blob_layout.Format() != GetBlobLayoutFormat(Info())) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  auto inode_block = NewBlob(blob_info.GetDigest());
  if (inode_block.is_error()) {
    FX_LOGS(ERROR) << "error: Failed to allocate a new blob " << inode_block.status_value();
    return inode_block.take_error();
  }

  Inode* inode = inode_block->GetInode();
  inode->blob_size = blob_layout.FileSize();
  inode->block_count = blob_layout.TotalBlockCount();
  inode->header.flags |=
      kBlobFlagAllocated |
      (blob_info.IsCompressed() ? HostCompressor::InodeHeaderCompressionFlags() : 0);

  // TODO(fxbug.rev/74008) Currently, host-side tools can only generate single-extent blobs. This
  // should be fixed.
  if (inode->block_count > kBlockCountMax) {
    FX_LOGS(ERROR) << "error: Blobs larger than " << kBlockCountMax
                   << " blocks not yet implemented";
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  if (inode->block_count > 0) {
    auto start_block = AllocateBlocks(inode->block_count);
    if (start_block.is_error()) {
      FX_LOGS(ERROR) << "error: No blocks available " << inode->block_count;
      return start_block.take_error();
    }
    inode->extents[0].SetStart(start_block.value());
    inode->extents[0].SetLength(static_cast<BlockCountType>(inode->block_count));
    inode->extent_count = 1;
  } else {
    inode->extent_count = 0;
  }

  if (zx::status<> status = WriteData(inode, blob_info); status.is_error()) {
    FX_LOGS(ERROR) << "Blobfs WriteData failed " << status.status_value();
    return status;
  }
  if (zx::status<> status = WriteBitmap(inode->extents[0].Start(), inode->block_count);
      status.is_error()) {
    FX_LOGS(ERROR) << "Blobfs WriteBitmap failed " << status.status_value();
    return status;
  }
  if (zx::status<> status = WriteNode(std::move(inode_block).value()); status.is_error()) {
    FX_LOGS(ERROR) << "Blobfs WriteNode failed " << status.status_value();
    return status;
  }
  if (zx::status<> status = WriteInfo(); status.is_error()) {
    FX_LOGS(ERROR) << "Blobfs WriteInfo failed " << status.status_value();
    return status;
  }

  return zx::ok();
}

zx::status<uint64_t> Blobfs::AllocateBlocks(uint64_t block_count) {
  size_t start_block;
  if (zx_status_t status = block_map_.Find(false, 0, block_map_.size(), block_count, &start_block);
      status != ZX_OK) {
    return zx::error(status);
  }
  if (zx_status_t status = block_map_.Set(start_block, start_block + block_count);
      status != ZX_OK) {
    return zx::error(status);
  }

  info_.alloc_block_count += block_count;
  return zx::ok(start_block);
}

zx::status<> Blobfs::WriteBitmap(uint64_t data_start_block, uint64_t data_block_count) {
  uint64_t block_bitmap_start_block = data_start_block / kBlobfsBlockBits;
  uint64_t block_bitmap_end_block =
      fbl::round_up(data_start_block + data_block_count, kBlobfsBlockBits) / kBlobfsBlockBits;
  const void* bmstart = block_map_.StorageUnsafe()->GetData();
  const void* data = fs::GetBlock(kBlobfsBlockSize, bmstart, block_bitmap_start_block);
  uint64_t absolute_block_number = block_map_start_block_ + block_bitmap_start_block;
  uint64_t block_count = block_bitmap_end_block - block_bitmap_start_block;
  return WriteBlocks(absolute_block_number, block_count, data);
}

zx::status<> Blobfs::WriteNode(std::unique_ptr<InodeBlock> ino_block) {
  size_t inode_block_index =
      (ino_block->GetBlockNumber() - NodeMapStartBlock(info_)) * kBlobfsInodesPerBlock;
  return WriteBlock(ino_block->GetBlockNumber(), &nodes_[inode_block_index]);
}

zx::status<> Blobfs::WriteData(Inode* inode, const BlobInfo& blob_info) {
  const BlobLayout& blob_layout = blob_info.GetBlobLayout();
  if (blob_layout.TotalBlockCount() == 0) {
    // Nothing to write.
    return zx::ok();
  }
  // Allocate a new buffer to hold both the data and Merkle tree together.  The data and Merkle tree
  // may not be block multiples in size which makes writing them separately in terms of blocks
  // difficult, also the data and Merkle tree may share a block.  Creating a new buffer to hold both
  // uses more memory but makes writing the blob significantly easier.
  uint64_t block_size = GetBlockSize();
  uint64_t buf_size = block_size * blob_layout.TotalBlockCount();
  auto buf = std::make_unique<uint8_t[]>(size_t{blob_layout.TotalBlockCount()} * GetBlockSize());
  // Zero the entire buffer instead of trying to calculate where the data and Merkle tree won't be.
  memset(buf.get(), 0, buf_size);

  // Copy the data to the buffer.
  uint64_t data_offset = block_size * blob_layout.DataBlockOffset();
  fbl::Span<const uint8_t> data = blob_info.GetData();
  memcpy(buf.get() + data_offset, data.data(), data.size());

  // |merkle_data| will be null when the blob size is less than or equal to the Merkle tree node
  // size.
  fbl::Span<const uint8_t> merkle_tree = blob_info.GetMerkleTree();
  if (!merkle_tree.empty()) {
    // Copy the Merkle tree to the buffer.
    uint64_t merkle_offset = block_size * blob_layout.MerkleTreeBlockOffset() +
                             blob_layout.MerkleTreeOffsetWithinBlockOffset();
    memcpy(buf.get() + merkle_offset, merkle_tree.data(), merkle_tree.size());
  }

  uint32_t blob_start_block = data_start_block_ + inode->extents[0].Start();
  if (zx::status<> status = WriteBlocks(blob_start_block, blob_layout.TotalBlockCount(), buf.get());
      status.is_error()) {
    FX_LOGS(ERROR) << "Failed to write a blob: " << status.status_value();
    return status;
  }
  return zx::ok();
}

zx::status<> Blobfs::WriteInfo() { return WriteBlock(0, info_block_); }

zx::status<> Blobfs::ReadBlocks(uint64_t start_block, uint64_t block_count, void* data) {
  return ReadBlocksWithOffset(blockfd_.get(), start_block, block_count, offset_, data);
}

zx::status<> Blobfs::ReadBlock(uint64_t block_number, void* data) {
  return ReadBlocks(block_number, /*block_count=*/1, data);
}

zx::status<> Blobfs::WriteBlocks(uint64_t start_block, uint64_t block_count, const void* data) {
  return WriteBlocksWithOffset(blockfd_.get(), start_block, block_count, offset_, data);
}

zx::status<> Blobfs::WriteBlock(uint64_t block_number, const void* data) {
  return WriteBlocks(block_number, /*block_count=*/1, data);
}

zx::status<InodePtr> Blobfs::GetNode(uint32_t index) {
  if (index >= info_.inode_count) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return zx::ok(InodePtr(&nodes_[index], InodePtrDeleter(this)));
}

// TODO(smklein): Consider deduplicating the host and target allocation systems.
bool Blobfs::CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                                  uint64_t* first_unset) const {
  size_t unset_bit;
  bool allocated = block_map_.Get(start_block, end_block, &unset_bit);
  if (!allocated && first_unset != nullptr) {
    *first_unset = static_cast<uint64_t>(unset_bit);
  }
  return allocated;
}

zx::status<> Blobfs::ReadBlocksForInode(uint32_t node_index, uint64_t start_block,
                                        uint64_t block_count, uint8_t* data) {
  zx::status<AllocatedExtentIterator> extent_iterator_or =
      AllocatedExtentIterator::Create(this, node_index);
  if (extent_iterator_or.is_error()) {
    return extent_iterator_or.take_error();
  }
  BlockIterator iter(
      std::make_unique<AllocatedExtentIterator>(std::move(extent_iterator_or.value())));
  if (zx_status_t status = IterateToBlock(&iter, start_block); status != ZX_OK) {
    return zx::error(status);
  }
  std::vector<std::pair<uint64_t, uint32_t>> ranges;
  zx_status_t status =
      StreamBlocks(&iter, block_count, [&ranges](int64_t, uint64_t start, uint32_t length) {
        ranges.emplace_back(start, length);
        return ZX_OK;
      });
  if (status != ZX_OK) {
    return zx::error(status);
  }
  size_t block_offset = 0;
  for (auto range : ranges) {
    zx::status<> status = ReadBlocks(data_start_block_ + range.first, range.second,
                                     &data[block_offset * GetBlockSize()]);
    if (status.is_error()) {
      return status;
    }
    block_offset += range.second;
  }
  return zx::ok();
}

fpromise::result<std::vector<uint8_t>, std::string> Blobfs::LoadDataAndVerifyBlob(
    uint32_t node_index) {
  auto inode_ptr = GetNode(node_index);
  if (inode_ptr.is_error()) {
    return fpromise::error("Failed to get Inode index " + std::to_string(node_index) + ": " +
                           std::to_string(inode_ptr.status_value()));
  }
  Inode inode = *inode_ptr.value();
  const uint32_t block_size = GetBlockSize();
  zx_status_t status;
  auto make_error = [&](const std::string& error) {
    digest::Digest digest(inode.merkle_root_hash);
    auto digest_str = digest.ToString();
    return fpromise::error("Blob with merkle root hash of " +
                           std::string(digest_str.data(), digest_str.length()) +
                           " had errors. More specifically: " + error);
  };

  auto blob_layout =
      blobfs::BlobLayout::CreateFromInode(GetBlobLayoutFormat(Info()), inode, block_size);
  if (blob_layout.is_error()) {
    return make_error("Failed to create blob layout with status " +
                      std::to_string(blob_layout.status_value()));
  }

  std::vector<uint8_t> merkle_tree_blocks(blob_layout->MerkleTreeBlockAlignedSize(), 0);
  std::vector<uint8_t> data_blocks(blob_layout->DataBlockAlignedSize(), 0);
  if (blob_layout->MerkleTreeBlockAlignedSize() > 0) {
    if (zx::status<> status =
            ReadBlocksForInode(node_index, blob_layout->MerkleTreeBlockOffset(),
                               blob_layout->MerkleTreeBlockCount(), merkle_tree_blocks.data());
        status.is_error()) {
      return make_error("Failed to read in merkle tree blocks: " +
                        std::to_string(status.status_value()));
    }
  }
  if (blob_layout->DataBlockAlignedSize() > 0) {
    if (zx::status<> status = ReadBlocksForInode(node_index, blob_layout->DataBlockOffset(),
                                                 blob_layout->DataBlockCount(), data_blocks.data());
        status.is_error()) {
      return make_error("Failed to read in data blocks: " + std::to_string(status.status_value()));
    }
  }

  // Decompress the data if necessary.
  if (inode.header.flags & HostCompressor::InodeHeaderCompressionFlags()) {
    size_t file_size = inode.blob_size;
    std::vector<uint8_t> uncompressed_data(file_size, 0);
    HostDecompressor decompressor;
    if ((status = decompressor.Decompress(uncompressed_data.data(), &file_size, data_blocks.data(),
                                          blob_layout->DataSizeUpperBound())) != ZX_OK) {
      return make_error("Failed to decompress with status " + std::to_string(status));
    }
    if (file_size != inode.blob_size) {
      return make_error("Decompressed blob size of " + std::to_string(file_size) +
                        " mismatch with blob inode expected size of " +
                        std::to_string(inode.blob_size));
    }
    // Replace the compressed data with the uncompressed data.
    data_blocks = std::move(uncompressed_data);
  }

  // Verify the contents of the blob.
  uint8_t* merkle_tree_ptr =
      merkle_tree_blocks.empty()
          ? nullptr
          : &merkle_tree_blocks[blob_layout->MerkleTreeOffsetWithinBlockOffset()];
  MerkleTreeVerifier mtv;
  mtv.SetUseCompactFormat(blobfs::ShouldUseCompactMerkleTreeFormat(blob_layout->Format()));
  if ((status = mtv.SetDataLength(inode.blob_size)) != ZX_OK ||
      (status = mtv.SetTree(merkle_tree_ptr, mtv.GetTreeLength(), inode.merkle_root_hash,
                            sizeof(inode.merkle_root_hash))) != ZX_OK ||
      (status = mtv.Verify(data_blocks.data(), inode.blob_size, 0)) != ZX_OK) {
    return make_error("Verification failed with status " + std::to_string(status));
  }

  // Remove trailing block alignment.
  data_blocks.resize(inode.blob_size, 0);

  return fpromise::ok(std::move(data_blocks));
}

zx_status_t Blobfs::LoadAndVerifyBlob(uint32_t node_index) {
  auto load_result = LoadDataAndVerifyBlob(node_index);
  if (load_result.is_error()) {
    FX_LOGS(ERROR) << load_result.error();
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

uint32_t Blobfs::GetBlockSize() const { return Info().block_size; }

fpromise::result<void, std::string> Blobfs::VisitBlobs(BlobVisitor visitor) {
  for (uint64_t inode_index = 0, allocated_nodes = 0;
       inode_index < info_.inode_count && allocated_nodes < info_.alloc_inode_count;
       ++inode_index) {
    auto inode = GetNode(inode_index);
    if (inode.is_error()) {
      return fpromise::error("Failed to retrieve inode.");
    }
    if (!inode->header.IsAllocated()) {
      continue;
    }

    allocated_nodes++;
    auto load_result = LoadDataAndVerifyBlob(inode_index);
    if (load_result.is_error()) {
      return load_result.take_error_result();
    }
    BlobView view = {
        .merkle_hash = fbl::Span<const uint8_t>(inode->merkle_root_hash),
        .blob_contents = load_result.value(),
    };

    auto visitor_result = visitor(view);
    if (visitor_result.is_error()) {
      return visitor_result.take_error_result();
    }
  }
  return fpromise::ok();
}

fpromise::result<void, std::string> ExportBlobs(int output_dir, Blobfs& fs) {
  return fs.VisitBlobs([output_dir](Blobfs::BlobView view) -> fpromise::result<void, std::string> {
    uint8_t hash[digest::kSha256Length];
    memcpy(hash, view.merkle_hash.data(), digest::kSha256Length);
    auto blob_name = digest::Digest(hash).ToString();
    fbl::unique_fd file(openat(output_dir, blob_name.c_str(), O_CREAT | O_RDWR, 0644));
    if (!file.is_valid()) {
      return fpromise::error(
          "Failed to create blob file" + std::string(blob_name.c_str()) +
          "(merkle root digest) in output dir. More specifically: " + strerror(errno));
    }

    size_t written_bytes = 0;
    ssize_t write_result = 0;
    while (written_bytes < view.blob_contents.size()) {
      write_result = write(file.get(), &view.blob_contents[written_bytes],
                           view.blob_contents.size() - written_bytes);
      if (write_result < 0) {
        return fpromise::error(
            "Failed to write blob " + std::string(blob_name.c_str()) +
            "(merkle root digest) contents in output file. More specifically: " + strerror(errno));
      }
      written_bytes += write_result;
    }

    return fpromise::ok();
  });
}

zx::status<std::unique_ptr<Superblock>> Blobfs::ReadBackupSuperblock() {
  auto superblock = std::make_unique<Superblock>();
  if (zx::status<> status = ReadBlock(kFVMBackupSuperblockOffset, superblock.get());
      status.is_error()) {
    return status.take_error();
  }
  return zx::ok(std::move(superblock));
}

}  // namespace blobfs
