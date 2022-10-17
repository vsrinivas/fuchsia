// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/sysconfig.h"

namespace paver {

zx::result<size_t> SysconfigPartitionClient::GetBlockSize() {
  size_t size;
  auto status = zx::make_result(client_.GetPartitionSize(partition_, &size));
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(size);
}

zx::result<size_t> SysconfigPartitionClient::GetPartitionSize() {
  size_t size;
  auto status = zx::make_result(client_.GetPartitionSize(partition_, &size));
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(size);
}

zx::result<> SysconfigPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  return zx::make_result(client_.ReadPartition(partition_, vmo, 0));
}

zx::result<> SysconfigPartitionClient::Write(const zx::vmo& vmo, size_t size) {
  size_t partition_size;
  if (auto status = client_.GetPartitionSize(partition_, &partition_size); status != ZX_OK) {
    return zx::error(status);
  }

  if (size != partition_size) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return zx::make_result(client_.WritePartition(partition_, vmo, 0));
}

zx::result<> SysconfigPartitionClient::Trim() { return zx::error(ZX_ERR_NOT_SUPPORTED); }

zx::result<> SysconfigPartitionClient::Flush() { return zx::ok(); }

fidl::ClientEnd<fuchsia_hardware_block::Block> SysconfigPartitionClient::GetChannel() { return {}; }

fbl::unique_fd SysconfigPartitionClient::block_fd() { return fbl::unique_fd(); }

}  // namespace paver
