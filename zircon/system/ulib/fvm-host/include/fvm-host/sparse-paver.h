// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_HOST_SPARSE_PAVER_H_
#define FVM_HOST_SPARSE_PAVER_H_

#include <memory>

#include <fvm-host/file-wrapper.h>
#include <fvm/sparse-reader.h>

#include "format.h"
#include "fvm-host/fvm-info.h"

struct SparsePartitionInfo {
  fvm::PartitionDescriptor descriptor;
  fbl::Vector<fvm::ExtentDescriptor> extents;
  std::unique_ptr<Format> format;
};

// Given a target path and partition data from a SparseReader, generates a full FVM image.
class SparsePaver {
 public:
  // Creates a SparsePaver with the given attributes.
  static zx_status_t Create(std::unique_ptr<fvm::host::FileWrapper> wrapper, size_t slice_size,
                            size_t disk_offset, size_t disk_size,
                            std::unique_ptr<SparsePaver>* out);

  // Allocates the partition and slices described by |partition| to info_, and writes out
  // corresponding data from |reader| to the FVM. |partition| will not be modified.
  zx_status_t AddPartition(const SparsePartitionInfo* partition, fvm::SparseReader* reader);

  // Commits the FVM image by writing the metadata to disk.
  zx_status_t Commit();

 private:
  SparsePaver(size_t disk_offset, size_t disk_size)
      : disk_offset_(disk_offset), disk_size_(disk_size) {}

  // Initializes the FVM metadata.
  zx_status_t Init(std::unique_ptr<fvm::host::FileWrapper> wrapper, size_t slice_size);

  // Allocates the extent described by |extent| to the partition at |vpart_index|, as well as
  // allocating its slices and persisting all associated data.
  zx_status_t AddExtent(uint32_t vpart_index, fvm::ExtentDescriptor* extent,
                        fvm::SparseReader* reader);

  // Writes the next slice out to disk, reading as many of |bytes_left| as possible from |reader|
  // and appending zeroes if necessary.
  zx_status_t WriteSlice(size_t* bytes_left, fvm::SparseReader* reader);

  FvmInfo info_;
  std::unique_ptr<fvm::host::FileWrapper> file_;
  size_t disk_offset_;               // Offset into fd_ at which to create FVM.
  size_t disk_size_;                 // Number of bytes allocated for the FVM.
  size_t disk_ptr_;                  // Marks the current offset within the target image.
  std::unique_ptr<uint8_t[]> data_;  // Buffer to hold data to be written to disk.
};

#endif  // FVM_HOST_SPARSE_PAVER_H_
