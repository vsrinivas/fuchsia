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

// Public for testing.
fbl::unique_fd FvmPartitionFormat(const fbl::unique_fd& devfs_root, fbl::unique_fd partition_fd,
                                  const fvm::sparse_image_t& header, BindOption option,
                                  FormatResult* format_result = nullptr);

// Given an fd representing a "sparse FVM format", fill the FVM with the
// provided partitions described by |partition_fd|.
//
// Decides to overwrite or create new partitions based on the type
// GUID, not the instance GUID.
zx_status_t FvmStreamPartitions(std::unique_ptr<PartitionClient> partition_client,
                                std::unique_ptr<fvm::ReaderInterface> payload);

}  // namespace paver

#endif  // ZIRCON_SYSTEM_ULIB_PAVER_FVM_H_
