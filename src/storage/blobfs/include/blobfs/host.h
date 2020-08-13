// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes host-side functionality for accessing Blobfs.

#ifndef SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_HOST_H_
#define SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_HOST_H_

#ifdef __Fuchsia__
#error Host-only Header
#endif

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>
#include <optional>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/node-finder.h>
#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>

class JsonRecorder;

namespace blobfs {

// Merkle Tree information associated with a file.
struct MerkleInfo {
  // Merkle-Tree related information.
  digest::Digest digest;
  std::unique_ptr<uint8_t[]> merkle;

  // The path which generated this file, and a cached file length.
  fbl::String path;
  uint64_t length = 0;

  // Compressed blob data, if the blob is compressible.
  std::unique_ptr<uint8_t[]> compressed_data;
  uint64_t compressed_length = 0;
  bool compressed = false;

  uint64_t GetDataBlocks() const {
    uint64_t blob_size = compressed ? compressed_length : length;
    return fbl::round_up(blob_size, kBlobfsBlockSize) / kBlobfsBlockSize;
  }

  uint64_t GetDataSize() const { return compressed ? compressed_length : length; }
};

// A mapping of a file. Does not own the file.
class FileMapping {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(FileMapping);

  FileMapping() = default;

  ~FileMapping() { reset(); }

  void reset() {
    if (data_ != nullptr) {
      munmap(data_, length_);
      data_ = nullptr;
    }
  }

  zx_status_t Map(int fd) {
    reset();

    struct stat s;
    if (fstat(fd, &s) < 0) {
      return ZX_ERR_BAD_STATE;
    }
    data_ = mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data_ == nullptr) {
      return ZX_ERR_BAD_STATE;
    }
    length_ = s.st_size;
    return ZX_OK;
  }

  void* data() const { return data_; }

  uint64_t length() const { return length_; }

 private:
  void* data_ = nullptr;
  uint64_t length_ = 0;
};

union info_block_t {
  uint8_t block[kBlobfsBlockSize];
  Superblock info;
};

// Stores pointer to an inode's metadata and the matching block number
class InodeBlock {
 public:
  InodeBlock(size_t bno, Inode* inode, const Digest& digest) : bno_(bno) {
    inode_ = inode;
    digest.CopyTo(inode_->merkle_root_hash, sizeof(inode_->merkle_root_hash));
  }

  size_t GetBno() const { return bno_; }

  Inode* GetInode() { return inode_; }

 private:
  size_t bno_;
  Inode* inode_;
};

class Blobfs : public fbl::RefCounted<Blobfs>, public NodeFinder {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Blobfs);

  // Creates an instance of Blobfs from the file at |blockfd|.
  // The blobfs partition is expected to start at |offset| bytes into the file.
  static zx_status_t Create(fbl::unique_fd blockfd, off_t offset, const info_block_t& info_block,
                            const fbl::Array<size_t>& extent_lengths, std::unique_ptr<Blobfs>* out);

  ~Blobfs() override = default;

  // Checks to see if a blob already exists, and if not allocates a new node
  zx_status_t NewBlob(const Digest& digest, std::unique_ptr<InodeBlock>* out);

  // Allocate |nblocks| starting at |*blkno_out| in memory
  zx_status_t AllocateBlocks(size_t nblocks, size_t* blkno_out);

  zx_status_t WriteData(Inode* inode, const void* merkle_data, const void* blob_data,
                        uint64_t data_size);
  zx_status_t WriteBitmap(size_t nblocks, size_t start_block);
  zx_status_t WriteNode(std::unique_ptr<InodeBlock> ino_block);
  zx_status_t WriteInfo();

  // Access the |node_index|-th inode
  InodePtr GetNode(uint32_t node_index) final;

  NodeFinder* GetNodeFinder() { return this; }

  // TODO(smklein): Consider deduplicating the host and target allocation systems.
  bool CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                            uint64_t* first_unset = nullptr) const {
    size_t unset_bit;
    bool allocated = block_map_.Get(start_block, end_block, &unset_bit);
    if (!allocated && first_unset != nullptr) {
      *first_unset = static_cast<uint64_t>(unset_bit);
    }
    return allocated;
  }

 private:
  struct BlockCache {
    size_t bno;
    uint8_t blk[kBlobfsBlockSize];
  };

  friend class BlobfsChecker;

  Blobfs(fbl::unique_fd fd, off_t offset, const info_block_t& info_block,
         const fbl::Array<size_t>& extent_lengths);
  zx_status_t LoadBitmap();

  // Read data from block |bno| into the block cache.
  // If the block cache already contains data from the specified bno, nothing happens.
  // Cannot read while a dirty block is pending.
  zx_status_t ReadBlock(size_t bno);

  // Write |block_count| blocks of |data| at block number starting at |block_number|.
  zx_status_t WriteBlocks(size_t block_number, uint64_t block_count, const void* data);

  // Write |data| into block |bno|
  zx_status_t WriteBlock(size_t bno, const void* data);

  zx_status_t ResetCache();

  zx_status_t LoadAndVerifyBlob(uint32_t node_index);

  RawBitmap block_map_{};

  fbl::unique_fd blockfd_;
  bool dirty_ = false;
  off_t offset_;

  size_t block_map_start_block_;
  size_t node_map_start_block_;
  size_t journal_start_block_;
  size_t data_start_block_;

  size_t block_map_block_count_;
  size_t node_map_block_count_;
  size_t journal_block_count_;
  size_t data_block_count_;

  union {
    Superblock info_;
    uint8_t info_block_[kBlobfsBlockSize];
  };

  // Caches the most recent block read from disk
  BlockCache cache_;
};

// Reads block |bno| into |data| from |fd|.
zx_status_t ReadBlock(int fd, uint64_t bno, void* data);

// Writes block |bno| from |data| into |fd|.
zx_status_t WriteBlock(int fd, uint64_t bno, const void* data);

// Returns the number of blobfs blocks that fit in |fd|.
zx_status_t GetBlockCount(int fd, uint64_t* out);

// Formats a blobfs filesystem, meant to contain |block_count| blobfs blocks, to
// the device represteted by |fd|.
//
// Returns -1 on error, 0 on success.
int Mkfs(int fd, uint64_t block_count);

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

// Pre-process a blob by creating a merkle tree and digest from the supplied file.
// Also return the length of the file. If |compress| is true and we decide to compress the file,
// the compressed length and data are returned.
zx_status_t blobfs_preprocess(int data_fd, bool compress, MerkleInfo* out_info);

// blobfs_add_blob may be called by multiple threads to gain concurrent
// merkle tree generation. No other methods are thread safe.
zx_status_t blobfs_add_blob(Blobfs* bs, JsonRecorder* json_recorder, int data_fd);

// Identical to blobfs_add_blob, but uses a precomputed Merkle Tree and digest.
zx_status_t blobfs_add_blob_with_merkle(Blobfs* bs, JsonRecorder* json_recorder, int data_fd,
                                        const MerkleInfo& info);

zx_status_t blobfs_fsck(fbl::unique_fd fd, off_t start, off_t end,
                        const fbl::Vector<size_t>& extent_lengths);

// Create a blobfs from a sparse file
// |start| indicates where the blobfs partition starts within the file (in bytes)
// |end| indicates the end of the blobfs partition (in bytes)
// |extent_lengths| contains the length (in bytes) of each blobfs extent: currently this includes
// the superblock, block bitmap, inode table, and data blocks.
zx_status_t blobfs_create_sparse(std::unique_ptr<Blobfs>* out, fbl::unique_fd fd, off_t start,
                                 off_t end, const fbl::Vector<size_t>& extent_vector);
}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_HOST_H_
