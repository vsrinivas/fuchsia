// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes host-side functionality for accessing Blobfs.

#ifndef SRC_STORAGE_BLOBFS_HOST_H_
#define SRC_STORAGE_BLOBFS_HOST_H_

#ifdef __Fuchsia__
#error Host-only Header
#endif

#include <assert.h>
#include <lib/fit/function.h>
#include <lib/fpromise/result.h>
#include <lib/stdcompat/span.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>

#include "src/lib/chunked-compression/multithreaded-chunked-compressor.h"
#include "src/lib/digest/digest.h"
#include "src/storage/blobfs/allocator/host_allocator.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {

// A mapping of a file. Does not own the file.
class FileMapping {
 public:
  FileMapping(const FileMapping&) = delete;
  FileMapping(FileMapping&& other) noexcept;
  FileMapping& operator=(const FileMapping&) = delete;
  FileMapping& operator=(FileMapping&& other) noexcept;
  ~FileMapping();

  static zx::result<FileMapping> Create(int fd);

  cpp20::span<const uint8_t> data() const {
    return cpp20::span(static_cast<const uint8_t*>(data_), length_);
  }

 private:
  FileMapping(void* data, uint64_t length) : data_(data), length_(length) {}

  void* data_ = nullptr;
  uint64_t length_ = 0;
};

// Merkle tree, data, and compression information associated with a file.
class BlobInfo {
 public:
  BlobInfo(const BlobInfo&) = delete;
  BlobInfo(BlobInfo&&) noexcept = default;
  BlobInfo& operator=(const BlobInfo&) = delete;
  BlobInfo& operator=(BlobInfo&&) noexcept = default;

  // Creates a BlobInfo object for |fd| using the layout specified by |blob_layout_format|. If
  // compressing the blob would save space then the blob will be compressed.
  static zx::result<BlobInfo> CreateCompressed(
      int fd, BlobLayoutFormat blob_layout_format, std::filesystem::path file_path,
      chunked_compression::MultithreadedChunkedCompressor& compressor);

  // Creates a BlobInfo object for |fd| using the layout specified by |blob_layout_format|. The blob
  // will not be compressed.
  static zx::result<BlobInfo> CreateUncompressed(int fd, BlobLayoutFormat blob_layout_format,
                                                 std::filesystem::path file_path);

  // If the blob was compressed then this function will return the compressed data. Otherwise the
  // uncompressed data is returned.
  cpp20::span<const uint8_t> GetData() const { return std::visit(BlobDataVisitor{}, blob_data_); }

  // Returns true if the data returned by |GetData| is compressed.
  bool IsCompressed() const { return std::holds_alternative<CompressedBlobData>(blob_data_); }

  const digest::Digest& GetDigest() const { return digest_; }
  cpp20::span<const uint8_t> GetMerkleTree() const {
    return cpp20::span<const uint8_t>(merkle_tree_);
  }
  const BlobLayout& GetBlobLayout() const { return *blob_layout_; }
  const std::filesystem::path& GetSrcFilePath() const { return src_file_path_; }

 private:
  BlobInfo() = default;

  digest::Digest digest_;
  std::vector<uint8_t> merkle_tree_;
  std::unique_ptr<BlobLayout> blob_layout_;

  // The path to the file which this blob came from.
  std::filesystem::path src_file_path_;

  using CompressedBlobData = std::vector<uint8_t>;
  using UncompressedBlobData = FileMapping;
  struct BlobDataVisitor {
    cpp20::span<const uint8_t> operator()(const std::monostate& /*unused*/) {
      ZX_PANIC("Blob data was not set");
    }
    cpp20::span<const uint8_t> operator()(const CompressedBlobData& compressed_data) {
      return cpp20::span<const uint8_t>(compressed_data);
    }
    cpp20::span<const uint8_t> operator()(const UncompressedBlobData& file_mapping) {
      return file_mapping.data();
    }
  };
  std::variant<std::monostate, CompressedBlobData, UncompressedBlobData> blob_data_;
};

union info_block_t {
  uint8_t block[kBlobfsBlockSize];
  Superblock info;
};

class Blobfs : public fbl::RefCounted<Blobfs> {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Blobfs);

  struct BlobView {
    cpp20::span<const uint8_t> merkle_hash;
    cpp20::span<const uint8_t> blob_contents;
  };

  using BlobVisitor = fit::function<fpromise::result<void, std::string>(BlobView)>;

  // Creates an instance of Blobfs from the file at |blockfd|. The blobfs partition is expected to
  // start at |offset| bytes into the file.
  static zx::result<std::unique_ptr<Blobfs>> Create(fbl::unique_fd blockfd, off_t offset,
                                                    const info_block_t& info_block,
                                                    const fbl::Array<size_t>& extent_lengths);

  // Adds a blob to the filesystem.
  zx::result<> AddBlob(const BlobInfo& blob_info);

  // Access the |node_index|-th inode.
  zx::result<InodePtr> GetNode(uint32_t node_index);

  NodeFinder* GetNodeFinder() { return allocator_.get(); }

  bool CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                            uint64_t* first_unset = nullptr) const;

  const Superblock& Info() const { return info_; }

  uint64_t GetBlockSize() const;

  // Calls |visitor| on each of the existing blobs. Errors on |visitor| will be forwarded to the
  // caller, and will stop the iteration.
  fpromise::result<void, std::string> VisitBlobs(BlobVisitor visitor);

  zx::result<std::unique_ptr<Superblock>> ReadBackupSuperblock();

 private:
  friend class BlobfsChecker;

  Blobfs(fbl::unique_fd fd, off_t offset, const info_block_t& info_block,
         const fbl::Array<size_t>& extent_lengths);

  // Finds the index of the inode with the given |digest|. Returns ZX_ERR_NOT_FOUND if there is no
  // inode with that digest.
  zx::result<uint32_t> FindInodeByDigest(const Digest& digest);

  zx::result<> WriteData(const BlobInfo& blob_info, const std::vector<ReservedExtent>& extents);
  zx::result<> WriteBlockBitmap(const Extent& extent);
  zx::result<> WriteNode(uint32_t node_index);
  zx::result<> WriteInfo();

  zx::result<RawBitmap> LoadBlockBitmap();

  zx::result<std::vector<Inode>> LoadNodeMap();

  // Read |block_count| blocks starting at |start_block| into |data|.
  zx::result<> ReadBlocks(uint64_t start_block, uint64_t block_count, void* data);

  // Read data from block |block_number| into |data|.
  zx::result<> ReadBlock(uint64_t block_number, void* data);

  // Read for inode |node_index| for |block_count| blocks from local |start_block| into |data|.
  zx::result<> ReadBlocksForInode(uint32_t node_index, uint64_t start_block, uint64_t block_count,
                                  uint8_t* data);

  // Write |block_count| blocks of |data| at block number starting at |start_block|.
  zx::result<> WriteBlocks(uint64_t start_block, uint64_t block_count, const void* data);

  // Write |data| into block |block_number|.
  zx::result<> WriteBlock(uint64_t block_number, const void* data);

  zx_status_t LoadAndVerifyBlob(uint32_t node_index);
  fpromise::result<std::vector<uint8_t>, std::string> LoadDataAndVerifyBlob(uint32_t inode_index);

  std::vector<Inode> node_map_;
  std::unique_ptr<HostAllocator> allocator_;

  fbl::unique_fd blockfd_;
  off_t offset_;

  uint64_t block_map_start_block_;
  uint64_t node_map_start_block_;
  uint64_t journal_start_block_;
  uint64_t data_start_block_;

  uint64_t block_map_block_count_;
  uint64_t node_map_block_count_;
  uint64_t journal_block_count_;
  uint64_t data_block_count_;

  union {
    Superblock info_;
    uint8_t info_block_[kBlobfsBlockSize];
  };
};

// Reads block |block_number| into |data| from |fd|.
zx_status_t ReadBlock(int fd, uint64_t block_number, void* data);

// Returns the number of blobfs blocks that fit in |fd|.
zx_status_t GetBlockCount(int fd, uint64_t* out);

// Formats a blobfs filesystem, meant to contain |block_count| blobfs blocks, to the device
// represented by |fd|.
//
// Returns -1 on error, 0 on success.
int Mkfs(int fd, uint64_t block_count, const FilesystemOptions& options);

// Copies into |out_size| the number of bytes used by data in fs contained in a partition between
// bytes |start| and |end|. If |start| and |end| are not passed, start is assumed to be zero and
// no safety checks are made for size of partition.
zx_status_t UsedDataSize(const fbl::unique_fd& fd, uint64_t* out_size, off_t start = 0,
                         std::optional<off_t> end = std::nullopt);

// Copies into |out_inodes| the number of allocated inodes in fs contained in a partition
// between bytes |start| and |end|.  If |start| and |end| are not passed, start is assumed to be
// zero and no safety checks are made for size of partition.
zx_status_t UsedInodes(const fbl::unique_fd& fd, uint64_t* out_inodes, off_t start = 0,
                       std::optional<off_t> end = std::nullopt);

// Copies into |out_size| the number of bytes used by data and bytes reserved for superblock,
// bitmaps, inodes and journal on fs contained in a partition between bytes |start| and |end|.
// If |start| and |end| are not passed, start is assumed to be zero and no safety checks are made
// for size of partition.
zx_status_t UsedSize(const fbl::unique_fd& fd, uint64_t* out_size, off_t start = 0,
                     std::optional<off_t> end = std::nullopt);

zx_status_t blobfs_create(std::unique_ptr<Blobfs>* out, fbl::unique_fd blockfd);

zx_status_t blobfs_fsck(fbl::unique_fd fd, off_t start, off_t end,
                        const std::vector<size_t>& extent_lengths);

// Create a blobfs from a sparse file.
// |start| indicates where the blobfs partition starts within the file (in bytes).
// |end| indicates the end of the blobfs partition (in bytes).
// |extent_lengths| contains the length (in bytes) of each blobfs extent: currently this includes
// the superblock, block bitmap, inode table, and data blocks.
zx_status_t blobfs_create_sparse(std::unique_ptr<Blobfs>* out, fbl::unique_fd fd, off_t start,
                                 off_t end, const std::vector<size_t>& extent_vector);

// Write each blob contained in this image into |output_dir| as a standalone file, with the merkle
// root hash as the filename.
fpromise::result<void, std::string> ExportBlobs(int output_dir, Blobfs& fs);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_HOST_H_
