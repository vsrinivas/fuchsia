// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_HOST_CONTAINER_H_
#define SRC_STORAGE_FVM_HOST_CONTAINER_H_

#include <fcntl.h>
#include <lib/zx/status.h>
#include <string.h>

#include <memory>
#include <variant>

#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <lz4/lz4frame.h>

#include "fbl/macros.h"
#include "src/storage/fvm/fvm_sparse.h"
#include "src/storage/fvm/host/file_wrapper.h"
#include "src/storage/fvm/host/format.h"
#include "src/storage/fvm/host/fvm_info.h"
#include "src/storage/fvm/host/sparse_paver.h"
#include "src/storage/fvm/sparse_reader.h"

// The number of additional slices a partition will need to become zxcrypt'd.
// TODO(planders): Replace this with a value supplied by ulib/zxcrypt.
constexpr size_t kZxcryptExtraSlices = 1;

// A Container represents a method of storing multiple file system partitions in an
// FVM-recognizable format
class Container {
 public:
  // Returns a Container representation of an existing FVM or sparse container starting at |
  // offset| within |path| (where offset is only valid for an FVM). Returns an error if the file
  // does not exist or is not a valid Container type, or if flags is not zero or a valid
  // combination of fvm::SparseFlags.
  static zx_status_t Create(const char* path, off_t offset, uint32_t flags,
                            std::unique_ptr<Container>* out);

  Container(const char* path, size_t slice_size, uint32_t flags);

  virtual ~Container();

  // Reports various information about the Container, e.g. number of partitions, and runs fsck on
  // all supported partitions (blobfs, minfs)
  virtual zx_status_t Verify() const = 0;

  // Commits the Container data to disk
  virtual zx_status_t Commit() = 0;

  // Returns the Container's specified slice size (in bytes)
  virtual size_t SliceSize() const = 0;

  // Given a path to a valid file system partition, adds that partition to the container
  virtual zx_status_t AddPartition(const char* path, const char* type_name,
                                   FvmReservation* reserve) = 0;

  // Adds a partition to store snapshot metadata. This must be called if any partitions enable A/B
  // snapshots, a condition which is checked before finalizing the image.
  // Must be called at most once.
  virtual zx_status_t AddSnapshotMetadataPartition(size_t reserved_slices) = 0;

  // Creates a partition of a given size and type, rounded to nearest slice. This is,
  // will allocate minimum amount of slices and the rest for the data region.
  virtual zx_status_t AddCorruptedPartition(const char* type, uint64_t required_size) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Calculates the minimum disk size required to hold the unpacked contents of the container.
  virtual uint64_t CalculateDiskSize() const = 0;

 protected:
  // Returns the minimum disk size necessary to store |slice_count| slices of size |slice_size_|
  // in an FVM.
  uint64_t CalculateDiskSizeForSlices(size_t slice_count) const;

  fbl::StringBuffer<PATH_MAX> path_;
  fbl::unique_fd fd_;
  size_t slice_size_;
  uint32_t flags_;
};

class CompressionContext {
 public:
  static fit::result<CompressionContext, std::string> Create();

  explicit CompressionContext() = default;
  CompressionContext(const CompressionContext&) = delete;

  CompressionContext(CompressionContext&& other) noexcept
      : cctx_(std::exchange(other.cctx_, nullptr)),
        data_(std::move(other.data_)),
        size_(other.size_),
        offset_(other.offset_) {}

  CompressionContext& operator=(CompressionContext&& other) noexcept {
    cctx_ = std::exchange(other.cctx_, nullptr);
    data_ = std::move(other.data_);
    size_ = other.size_;
    offset_ = other.offset_;
    return *this;
  }

  ~CompressionContext() {
    // Perform a final freeing of the compression context to make sure memory is deallocated.
    LZ4F_errorCode_t errc = LZ4F_freeCompressionContext(cctx_);
    if (LZ4F_isError(errc)) {
      fprintf(stderr, "Could not free compression context: %s\n", LZ4F_getErrorName(errc));
    }
  }

  zx_status_t Setup(size_t max_len);
  zx_status_t Compress(const void* data, size_t length);
  zx_status_t Finish();

  const void* GetData() const { return data_.get(); }
  size_t GetLength() const { return offset_; }

 private:
  void IncreaseOffset(size_t value) {
    offset_ += value;
    ZX_DEBUG_ASSERT(offset_ <= size_);
  }

  size_t GetRemaining() const { return size_ - offset_; }

  void* GetBuffer() const { return data_.get() + offset_; }

  void Reset(size_t size) {
    data_.reset(new uint8_t[size]);
    size_ = size;
    offset_ = 0;
  }

  LZ4F_compressionContext_t cctx_ = nullptr;
  std::unique_ptr<uint8_t[]> data_;
  size_t size_ = 0;
  size_t offset_ = 0;
};

#endif  // SRC_STORAGE_FVM_HOST_CONTAINER_H_
