// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_HOST_SPARSE_CONTAINER_H_
#define SRC_STORAGE_FVM_HOST_SPARSE_CONTAINER_H_

#include "src/storage/fvm/host/container.h"

// Function pointer type which operates on partitions that ranges between [|start|, |end|).
// extent_lengths are lengths of each extents in bytes. |out| contains a unit which is dependent on
// the function called.
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

  // On success, returns ZX_OK and copies the number of bytes used by data within the fs.
  zx_status_t UsedDataSize(uint64_t* out_size) const;

  // On success, returns ZX_OK and copies the number allocated inodes within the fs.
  zx_status_t UsedInodes(uint64_t* out_inodes) const;

  // On success, returns ZX_OK and copies the number of bytes used by data and bytes reserved for
  // superblock, bitmaps, inodes and journal within the fs.
  zx_status_t UsedSize(uint64_t* out_size) const;
  zx_status_t Commit() final;

  // Unpacks the sparse container and "paves" it to the file system exposed by |wrapper|.
  zx_status_t Pave(std::unique_ptr<fvm::host::FileWrapper> wrapper, size_t disk_offset = 0,
                   size_t disk_size = 0);

  size_t SliceSize() const final;
  size_t SliceCount() const;
  zx_status_t AddPartition(const char* path, const char* type_name, FvmReservation* reserve) final;
  zx_status_t AddSnapshotMetadataPartition(size_t reserved_slices) final;

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
  zx_status_t AllocateExtent(uint32_t part_index, fvm::ExtentDescriptor extent);

  zx_status_t PrepareWrite(size_t max_len);
  zx_status_t WriteData(const void* data, size_t length);
  zx_status_t WriteZeroes(size_t length);
  zx_status_t CompleteWrite();

  // Calls |used_size_f| on fvm partitions that contain a successfully detected format (through
  // Format::Detect()). |out| has a unit which is dependent on the function called.
  zx_status_t PartitionsIterator(UsedSize_f* used_size_f, uint64_t* out) const;

  void CheckValid() const;

  fvm::Header GetFvmConfiguration(uint64_t target_disk_size) const;
};

#endif  // SRC_STORAGE_FVM_HOST_SPARSE_CONTAINER_H_
