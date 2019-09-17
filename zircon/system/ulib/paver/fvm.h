// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <fbl/unique_ptr.h>
#include <fvm/sparse-reader.h>

#include <block-client/cpp/client.h>

namespace paver {

// Options for locating an FVM within a partition.
enum class BindOption {
  // Bind to the FVM, if it exists already.
  TryBind,
  // Reformat the partition, regardless of if it already exists as an FVM.
  Reformat,
};

// Public for testing.
fbl::unique_fd FvmPartitionFormat(const fbl::unique_fd& devfs_root, fbl::unique_fd partition_fd,
                                  size_t slice_size, BindOption option);

// Registers a FIFO
zx_status_t RegisterFastBlockIo(const fbl::unique_fd& fd, const zx::vmo& vmo, vmoid_t* out_vmoid,
                                block_client::Client* out_client);

// Given an fd representing a "sparse FVM format", fill the FVM with the
// provided partitions described by |partition_fd|.
//
// Decides to overwrite or create new partitions based on the type
// GUID, not the instance GUID.
zx_status_t FvmStreamPartitions(fbl::unique_fd partition_fd,
                                std::unique_ptr<fvm::ReaderInterface> payload);

}  // namespace paver
