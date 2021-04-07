// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_ADAPTER_ADAPTER_OPTIONS_H_
#define SRC_STORAGE_VOLUME_IMAGE_ADAPTER_ADAPTER_OPTIONS_H_

#include <cstdint>
#include <optional>

namespace storage::volume_image {

struct PartitionOptions {
  // The generated partition, must contain enough bytes in the mapping containing inodes to host
  // |min_inode_count| at least. This is a lowerbound for the size of the mapping.
  std::optional<uint64_t> min_inode_count;

  // The generated partition, must contain enough bytes in the mapping containing data to host
  // |min_data_bytes| at least. This is a lowerbound for the size of the mapping.
  std::optional<uint64_t> min_data_bytes;

  // Maximum number of bytes a partition may use.
  //
  // E.g.
  // * Blobfs Partition will increase its journal size based on the remaining slices.
  // * Empty Partition will allocate this amount of slices starting from target offset 0.
  std::optional<uint64_t> max_bytes;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_ADAPTER_ADAPTER_OPTIONS_H_
