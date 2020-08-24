// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_FVM_SPARSE_H_
#define FVM_FVM_SPARSE_H_

#include <stdlib.h>

#include "format.h"

namespace fvm {

// This file describes the format for a "sparse FVM format",
// which attempts to densely pack an FVM-formatted partition
// onto a contiguous image. This format is intended to be used
// to stream FVM images between devices.
//
// The format of a sparse FVM image is as follows:
// HEADER:
// - sparse_image_t, followed by |partition_count| entries of...
//   - partition_descriptor_t, followed by |extent_count| entries of...
//      - extent_descriptor_t
// DATA:
// - All the previously mentioned extents, in order.
//
// For example,
//
// HEADER:
//   sparse_image_t
//      Partition descriptor 0
//        Extent descriptor 0
//        Extent descriptor 1
//        Extent descriptor 2
//      Partition descriptor 1
//        Extent descriptor 0
//      Partition descriptor 2
//        Extent descriptor 0
// DATA:
//   P0, Extent 0
//   P0, Extent 1
//   P0, Extent 2
//   P1, Extent 0
//   P2, Extent 0

constexpr uint64_t kSparseFormatMagic = 0x53525053204d5646ull;  // 'FVM SPRS'
constexpr uint64_t kSparseFormatVersion = 0x3;

enum SparseFlags {
  kSparseFlagLz4 = 0x1,
  kSparseFlagZxcrypt = 0x2,
  // Marks a partition as intentionaly corrupted.
  kSparseFlagCorrupted = 0x4,
  // If set, indicates zero filling is not required which is otherwise expected for extents where
  // extent_length < slice_count.
  kSparseFlagZeroFillNotRequired = 0x8,
  // The final value is the bitwise-OR of all other flags
  kSparseFlagAllValid =
      kSparseFlagLz4 | kSparseFlagZxcrypt | kSparseFlagCorrupted | kSparseFlagZeroFillNotRequired,
};

struct __attribute__((packed)) SparseImage {
  uint64_t magic;
  uint64_t version;
  uint64_t header_length;
  uint64_t slice_size;  // Unit: Bytes
  uint64_t partition_count;
  // Size in bytes for the maximum disk size this fvm image will reference.
  // If set to 0, will use the disk size at format time as the maximum size.
  // The initial size is always the size of the block device being formatted.
  uint64_t maximum_disk_size;
  uint32_t flags;
};

constexpr uint64_t kPartitionDescriptorMagic = 0x0bde4df7cf5c4c5dull;

struct __attribute__((packed)) PartitionDescriptor {
  uint64_t magic;
  uint8_t type[fvm::kGuidSize];
  uint8_t name[fvm::kMaxVPartitionNameLength];
  uint32_t flags;
  uint32_t extent_count;
};

constexpr uint64_t kExtentDescriptorMagic = 0xa5b8742906e8382eull;

struct __attribute__((packed)) ExtentDescriptor {
  uint64_t magic;
  uint64_t slice_start;    // Unit: slice
  uint64_t slice_count;    // Unit: slice
  uint64_t extent_length;  // Unit: bytes. Must be <= slice_count * slice_size.
};

}  // namespace fvm

#endif  // FVM_FVM_SPARSE_H_
