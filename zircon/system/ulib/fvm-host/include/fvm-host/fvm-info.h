// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_HOST_FVM_INFO_H_
#define FVM_HOST_FVM_INFO_H_

#include <memory>

#include <fbl/unique_fd.h>
#include <fvm-host/file-wrapper.h>
#include <fvm/fvm-sparse.h>

// Wrapper around FVM metadata which attempts to read existing metadata from disk, allows
// new partitions and slices to be allocated, and writes updated metadata back to disk.
class FvmInfo {
 public:
  FvmInfo() : valid_(false), dirty_(false), metadata_size_(0), vpart_hint_(1), pslice_hint_(1) {}

  // Resets the metadata to default values.
  zx_status_t Reset(size_t disk_size, size_t slice_size);

  // Loads and validates metadata from disk. If invalid metadata is found a success status is
  // returned, but valid_ is marked false.
  zx_status_t Load(fvm::host::FileWrapper* file, uint64_t disk_offset, uint64_t disk_size);

  // Validates the loaded metadata.
  zx_status_t Validate() const;

  // Grows in-memory metadata representation to the specified size.
  zx_status_t Grow(size_t metadata_size);

  // Grows in-memory metadata representation to account for |slice_count| additional slices.
  zx_status_t GrowForSlices(size_t slice_count);

  // Writes metadata to the file wrapped by |wrapper| of size |disk_size|, starting at offset
  // |disk_offset|.
  zx_status_t Write(fvm::host::FileWrapper* wrapper, size_t disk_offset, size_t disk_size);

  // Allocates new partition (in memory) with a single slice.
  zx_status_t AllocatePartition(const fvm::PartitionDescriptor* partition, uint8_t* guid,
                                uint32_t* vpart_index);

  // Allocates new slice for given partition (in memory).
  zx_status_t AllocateSlice(uint32_t vpart, uint32_t vslice, uint32_t* pslice);

  // Helpers to grab reference to partition/slice from metadata
  zx_status_t GetPartition(size_t index, fvm::vpart_entry_t** out) const;
  zx_status_t GetSlice(size_t index, fvm::slice_entry_t** out) const;

  fvm::Header* SuperBlock() const;
  size_t MetadataSize() const { return metadata_size_; }
  size_t DiskSize() const { return SuperBlock()->fvm_partition_size; }
  size_t SliceSize() const { return SuperBlock()->slice_size; }

  // Returns true if the in-memory metadata has been changed from the original values (i.e.
  // partitions/slices have been allocated since initialization).
  bool IsDirty() const { return dirty_; }

  // Returns true if metadata_ contains valid FVM metadata.
  bool IsValid() const { return valid_; }

  // Checks whether the metadata is valid, and immediately exits the process if it isn't.
  void CheckValid() const;

 private:
  bool valid_;
  bool dirty_;
  size_t metadata_size_;
  uint32_t vpart_hint_;
  uint32_t pslice_hint_;
  std::unique_ptr<uint8_t[]> metadata_;
};

#endif  // FVM_HOST_FVM_INFO_H_
