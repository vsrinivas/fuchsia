// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_FVM_H_
#define FVM_FVM_H_

#include <zircon/types.h>

#include <fvm/format.h>

namespace fvm {

// Helper class for obtaining information about the format of a FVM, such as superblock offsets,
// metadata size, allocated sizes, etc.
//
// This class is copyable assignable, and moveable.
class FormatInfo {
 public:
  // Returns a FormatInfo from a Fvm header and a disk size.
  static FormatInfo FromSuperBlock(const Header& superblock);

  // Assumes a superblock created from the given disk for the given disk size, with slice size. (No
  // Preallocated metadata headers for future growth).
  static FormatInfo FromDiskSize(size_t disk_size, size_t slice_size);

  // Without instantiating a SuperBlock, assumes that an fvm will be formatted initially with
  // |initial_size| and eventually will grow up to |max_size| with |slice_size|.
  static FormatInfo FromPreallocatedSize(size_t initial_size, size_t max_size, size_t slice_size);

  // Returns the size of the addressable metadata in a FVM header.
  size_t metadata_size() const { return metadata_size_; }

  // Returns the size of the allocated metadata SuperBlock. This may be bigger than |metadata_size_|
  // if there was extra space pre allocated for allowing fvm growing.
  size_t metadata_allocated_size() const { return metadata_allocated_size_; }

  // Returns the number of addressable slices for the superblock, this is the number of physical
  // slices.
  size_t slice_count() const { return slice_count_; }

  // Returns the size of each slice in the described block.
  size_t slice_size() const { return slice_size_; }

  // Returns the offset of the given superblock. The first superblock is considered primary, in
  // terms of position.
  size_t GetSuperblockOffset(SuperblockType type) const {
    return (type == SuperblockType::kPrimary) ? 0 : metadata_allocated_size();
  }

  // Returns the offset from the start of the disk to beginning of |pslice| physical slice.
  // Note: pslice is 1-indexed.
  size_t GetSliceStart(size_t pslice) const {
    return 2 * metadata_allocated_size_ + (pslice - 1) * slice_size_;
  }

  // Returns the maximum number of slices that can be addressed from the maximum possible size of
  // the metatadata.
  size_t GetMaxAllocatableSlices() const {
    return (metadata_allocated_size() - kAllocTableOffset) / sizeof(SliceEntry);
  }

  // Returns the maximum number of slices that the allocated metadata can address for a given
  // |disk_size|.
  size_t GetMaxAddressableSlices(uint64_t disk_size) const {
    size_t slice_count =
        std::min(GetMaxAllocatableSlices(), UsableSlicesCount(disk_size, slice_size_));
    // Because the allocation table is 1-indexed and pslices are 0 indexed on disk, if the number of
    // slices fit perfectly in the metadata, the allocated buffer won't be big enough to address
    // them all. This only happens when the rounded up block value happens to match the disk size.
    // TODO(gevalentino): Fix underlying cause and remove workaround.
    if ((AllocationTable::kOffset + slice_count * sizeof(SliceEntry)) ==
        metadata_allocated_size()) {
      slice_count--;
    }
    return slice_count;
  }

  // Returns the maximum partition size the current metadata can grow to.
  size_t GetMaxPartitionSize() const {
    return GetSliceStart(1) + GetMaxAllocatableSlices() * slice_size_;
  }

 private:
  // Size in bytes of addressable metadata.
  size_t metadata_size_ = 0;

  // Size in bytes of the allocated size of metadata.
  size_t metadata_allocated_size_ = 0;

  // Number of addressable slices.
  size_t slice_count_ = 0;

  // Size of the slices.
  size_t slice_size_ = 0;
};

// Update's the metadata's hash field to accurately reflect the contents of metadata.
void UpdateHash(void* metadata, size_t metadata_size);

// Validate the FVM header information, and identify which copy of metadata (primary or backup)
// should be used for initial reading, if either.
//
// "out" is an optional output parameter which is equal to a valid copy of either metadata or backup
// on success.
zx_status_t ValidateHeader(const void* metadata, const void* backup, size_t metadata_size,
                           const void** out);

}  // namespace fvm

#endif  // FVM_FVM_H_
