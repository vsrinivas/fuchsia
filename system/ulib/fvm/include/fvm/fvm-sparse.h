// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>

#include "fvm.h"

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

constexpr uint64_t kSparseFormatMagic = (0x53525053204d5646ull); // 'FVM SPRS'
constexpr uint64_t kSparseFormatVersion = 0x2;

constexpr uint32_t kSparseFlagLz4 = 0x1;

typedef struct sparse_image {
    uint64_t magic;
    uint64_t version;
    uint64_t header_length;
    uint64_t slice_size; // Unit: Bytes
    uint64_t partition_count;
    uint32_t flags;
} __attribute__((packed)) sparse_image_t;

constexpr uint64_t kPartitionDescriptorMagic = (0x0bde4df7cf5c4c5dull);

typedef struct partition_descriptor {
    uint64_t magic;
    uint8_t type[FVM_GUID_LEN];
    uint8_t name[FVM_NAME_LEN];
    uint32_t flags;
    uint32_t extent_count;
} __attribute__((packed)) partition_descriptor_t;

constexpr uint64_t kExtentDescriptorMagic = (0xa5b8742906e8382eull);

typedef struct extent_descriptor {
    uint64_t magic;
    uint64_t slice_start; // Unit: slice
    uint64_t slice_count; // Unit: slice
    uint64_t extent_length; // Unit: bytes. Must be <= slice_count * slice_size.
} __attribute__((packed)) extent_descriptor_t;

} // namespace fvm
