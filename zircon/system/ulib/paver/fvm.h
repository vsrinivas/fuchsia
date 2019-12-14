// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_ULIB_PAVER_FVM_H_
#define ZIRCON_SYSTEM_ULIB_PAVER_FVM_H_

#include <block-client/cpp/client.h>
#include <fvm/sparse-reader.h>

#include "fvm/fvm-sparse.h"
#include "partition-client.h"

namespace paver {

// Options for locating an FVM within a partition.
enum class BindOption {
  // Bind to the FVM, if it exists already.
  TryBind,
  // Reformat the partition, regardless of if it already exists as an FVM.
  Reformat,
};

// Describes the result of attempting to format an Fvm Partition.
enum class FormatResult {
  kUnknown,
  kPreserved,
  kReformatted,
};

// Attempts to bind an FVM driver to a partition fd. Returns a file descriptor
// for the FVM's device.
fbl::unique_fd TryBindToFvmDriver(const fbl::unique_fd& devfs_root,
                                  const fbl::unique_fd& partition_fd, zx::duration timeout);

// Formats the FVM within the provided partition if it is not already formatted.
// Returns a file descriptor for the FVM's device.
fbl::unique_fd FvmPartitionFormat(const fbl::unique_fd& devfs_root, fbl::unique_fd partition_fd,
                                  const fvm::sparse_image_t& header, BindOption option,
                                  FormatResult* format_result = nullptr);

// Allocates empty partitions inside the volume manager. Note that the partitions
// are simply allocated; the actual size of each partition (number of slices etc)
// is determined when formatting each volume.
zx_status_t AllocateEmptyPartitions(const fbl::unique_fd& devfs_root, const fbl::unique_fd& fvm_fd);

// Given an fd representing a "sparse FVM format", fill the FVM with the
// provided partitions described by |partition_fd|.
//
// Decides to overwrite or create new partitions based on the type
// GUID, not the instance GUID.
zx_status_t FvmStreamPartitions(const fbl::unique_fd& devfs_root,
                                std::unique_ptr<PartitionClient> partition_client,
                                std::unique_ptr<fvm::ReaderInterface> payload);

}  // namespace paver

#endif  // ZIRCON_SYSTEM_ULIB_PAVER_FVM_H_
