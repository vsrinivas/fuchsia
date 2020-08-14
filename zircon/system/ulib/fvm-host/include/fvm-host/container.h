// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_HOST_CONTAINER_H_
#define FVM_HOST_CONTAINER_H_

#include <fcntl.h>
#include <string.h>

#include <memory>
#include <variant>

#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fvm/fvm-sparse.h>
#include <fvm/sparse-reader.h>
#include <lz4/lz4frame.h>

#include "fbl/macros.h"
#include "file-wrapper.h"
#include "format.h"
#include "fvm-info.h"
#include "sparse-paver.h"

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

class FvmContainer final : public Container {
  struct FvmPartitionInfo {
    uint32_t vpart_index;
    uint32_t pslice_start;
    uint32_t slice_count;
    std::unique_ptr<Format> format;
  };

 public:
  enum class ExtendLengthType { EXACT, LOWER_BOUND };
  // Creates a new FvmContainer at the given |path|, regardless of whether one already exists.
  // Uses the provided |slice_size| to create the container starting at |offset| bytes within the
  // file with a total length of |length| bytes, and returns the result in |out|.
  static zx_status_t CreateNew(const char* path, size_t slice_size, off_t offset, off_t length,
                               std::unique_ptr<FvmContainer>* out);

  // Creates an FvmContainer from the existing image located at |offset| bytes within |path|.
  // Fails if a valid image  does not already exist.
  static zx_status_t CreateExisting(const char* path, off_t offset,
                                    std::unique_ptr<FvmContainer>* out);

  ~FvmContainer();

  // Resets the FvmContainer state so we are ready to add a new set of partitions
  // Init must be called separately from the constructor, as it will overwrite data pertinent to
  // an existing FvmContainer.
  zx_status_t Init();
  zx_status_t Verify() const final;

  zx_status_t Commit() final;

  // Extends the FVM container to the specified length
  zx_status_t Extend(size_t length);
  size_t SliceSize() const final;
  zx_status_t AddPartition(const char* path, const char* type_name, FvmReservation* reserve) final;

  uint64_t CalculateDiskSize() const final;

  void SetExtendLengthType(ExtendLengthType opt) { extend_length_type_ = opt; }

  // Returns the actual disk size.
  uint64_t GetDiskSize() const;
  // Trim the image file to only keep essential content.
  zx_status_t ResizeImageFileToFit();
  // Convert the image to android sparse format.
  zx_status_t ConvertToAndroidSparseImage();
  // Compress the image with lz4.
  zx_status_t CompressWithLZ4();
  // Add non-empty segment information, currently for test purpose.
  void AddNonEmptySegment(size_t start, size_t end);

 private:
  uint64_t disk_offset_;
  uint64_t disk_size_;
  fbl::Vector<FvmPartitionInfo> partitions_;
  FvmInfo info_;
  ExtendLengthType extend_length_type_ = ExtendLengthType::EXACT;

  struct Segment {
    size_t start;
    size_t end;
  };
  std::vector<Segment> non_empty_segments_;

  FvmContainer(const char* path, size_t slice_size, off_t offset, off_t length);

  // Resets the FvmContainer state so we are ready to add a new set of partitions.
  zx_status_t InitNew();

  // Reads fvm data from disk so we are able to inspect the existing container.
  zx_status_t InitExisting();

  // Verifies that the size of the existing file is valid based on the provided disk offset and
  // length. Optionally returns the file size as |size_out|.
  zx_status_t VerifyFileSize(uint64_t* size_out = nullptr, bool allow_resize = false);

  // Write the |part_index|th partition to disk
  zx_status_t WritePartition(unsigned part_index);
  // Write a partition's |extent_index|th extent to disk. |*pslice| is the starting pslice, and
  // is updated to reflect the latest written pslice.
  zx_status_t WriteExtent(unsigned extent_index, Format* format, uint32_t* pslice);
  // Write one data block of size |block_size| to disk at |block_offset| within pslice |pslice|
  zx_status_t WriteData(uint32_t pslice, uint32_t block_offset, size_t block_size, void* data);
  // Calculate total slices in added partitions.
  size_t CountAddedSlices() const;
  // The method returns the offset in bytes of a block in a slice
  size_t GetBlockStart(uint32_t pslice, uint32_t block_offset, size_t block_size) const;
  // Helper function to determine the type of android sparse image block type.
  AndroidSparseChunkType DetermineAndroidSparseChunkType(const uint32_t* buffer, size_t block_size,
                                                         size_t block_start);
  void FinalizeNonEmptySegmentsInfo();
};

class CompressionContext {
 public:
  static fit::result<CompressionContext, std::string> Create();
  explicit CompressionContext() = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(CompressionContext);
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

// Function pointer type which operates on partitions that ranges between [|start|, |end|).
// extent_lengths are lengths of each extents in bytes. |out| contains a unit
// which is dependent on the function called.
typedef zx_status_t(UsedSize_f)(const fbl::unique_fd& fd, off_t start, off_t end,
                                const fbl::Vector<size_t>& extent_lengths, disk_format_t part,
                                uint64_t* out);

class SparseContainer final : public Container {
 public:
  // Creates a new SparseContainer at the given |path|, regardless of whether one already exists.
  // Uses the provided |slice_size| and |flags| to create the container and returns the result in
  // |out|.
  static zx_status_t CreateNew(const char* path, size_t slice_size, uint32_t flags,
                               std::unique_ptr<SparseContainer>* out);

  // Creates a new SparseContainer at the given |path|, regardless of whether one already exists.
  // Uses the provided |slice_size|, |max_disk_size| and |flags| to create the container and returns
  // the result in |out|.
  static zx_status_t CreateNew(const char* path, size_t slice_size, uint32_t flags,
                               uint64_t max_disk_size, std::unique_ptr<SparseContainer>* out);

  // Creates a SparseContainer from the image located at |path|. Fails if a valid image does not
  // already exist.
  static zx_status_t CreateExisting(const char* path, std::unique_ptr<SparseContainer>* out);

  ~SparseContainer();

  // Returns the maximum disk size the FVM will be able to address. This allows preallocating
  // metadata storage when formatting an FVM.
  uint64_t MaximumDiskSize() const;

  zx_status_t Verify() const final;

  // On success, returns ZX_OK and copies the number of bytes used by data
  // within the fs.
  zx_status_t UsedDataSize(uint64_t* out_size) const;

  // On success, returns ZX_OK and copies the number allocated
  // inodes within the fs.
  zx_status_t UsedInodes(uint64_t* out_inodes) const;

  // On success, returns ZX_OK and copies the number of bytes used by data
  // and bytes reserved for superblock, bitmaps, inodes and journal within
  // the fs.
  zx_status_t UsedSize(uint64_t* out_size) const;
  zx_status_t Commit() final;

  // Unpacks the sparse container and "paves" it to the file system exposed by |wrapper|.
  zx_status_t Pave(std::unique_ptr<fvm::host::FileWrapper> wrapper, size_t disk_offset = 0,
                   size_t disk_size = 0);

  size_t SliceSize() const final;
  size_t SliceCount() const;
  zx_status_t AddPartition(const char* path, const char* type_name, FvmReservation* reserve) final;

  // Decompresses the contents of the sparse file (if they are compressed), and writes the output
  // to |path|.
  zx_status_t Decompress(const char* path);

  uint64_t CalculateDiskSize() const final;

  // Checks whether the container will fit within a disk of size |target_size| (in bytes).
  zx_status_t CheckDiskSize(uint64_t target_size) const;

  // Creates a partition of a given size and type, rounded to nearest slice. This is,
  // will allocate minimum amount of slices and the rest for the data region.
  zx_status_t AddCorruptedPartition(const char* type, uint64_t required_size) final;

 private:
  bool valid_;
  bool dirty_;
  size_t disk_size_;
  size_t extent_size_;
  fvm::SparseImage image_;
  fbl::Vector<SparsePartitionInfo> partitions_;
  CompressionContext compression_;
  std::unique_ptr<fvm::SparseReader> reader_;

  SparseContainer(const char* path, uint64_t slice_size, uint32_t flags);

  // Resets the SparseContainer state so we are ready to add a new set of partitions.
  zx_status_t InitNew();

  // Reads sparse data from disk so we are able to inspect the existing container.
  zx_status_t InitExisting();

  zx_status_t AllocatePartition(std::unique_ptr<Format> format, FvmReservation* reserve);
  zx_status_t AllocateExtent(uint32_t part_index, uint64_t slice_start, uint64_t slice_count,
                             uint64_t extent_length);

  zx_status_t PrepareWrite(size_t max_len);
  zx_status_t WriteData(const void* data, size_t length);
  zx_status_t CompleteWrite();
  // Calls |used_size_f| on fvm partitions that contain a successfully detected format (through
  // Format::Detect()). |out| has a unit which is dependent on the function called.
  zx_status_t PartitionsIterator(UsedSize_f* used_size_f, uint64_t* out) const;
  void CheckValid() const;
};

#endif  // FVM_HOST_CONTAINER_H_
