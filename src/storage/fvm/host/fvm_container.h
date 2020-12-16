// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_HOST_FVM_CONTAINER_H_
#define SRC_STORAGE_FVM_HOST_FVM_CONTAINER_H_

#include "src/storage/fvm/host/container.h"

class FvmContainer final : public Container {
  struct FvmPartitionInfo {
    uint32_t vpart_index = 0;
    uint32_t vslice_count = 0;
    uint32_t pslice_start = 0;
    uint32_t pslice_count = 0;
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

  // Verify if a given file contains an valid image located at |offset|.
  static zx_status_t Verify(const char* path, off_t offset);

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
  zx_status_t AddSnapshotMetadataPartition(size_t reserved_slices) final;

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

  enum class InitExistingMode {
    kCheckOnly,
    kAllowModification,
  };

  // Reads fvm data from disk so we are able to inspect the existing container.
  zx_status_t InitExisting(InitExistingMode mode = InitExistingMode::kAllowModification);

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
  // Counterpart of ResizeImageFileToFit(). It resizes the image file to be equal to the disk-size
  // specified in the header plus disk-offset.
  zx_status_t ResizeImageFileToDiskSize();
  void FinalizeNonEmptySegmentsInfo();
};

#endif  // SRC_STORAGE_FVM_HOST_FVM_CONTAINER_H_
