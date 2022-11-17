// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/partition-client.h"

#include <lib/component/incoming/cpp/service_client.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/status.h>

#include <cstdint>
#include <numeric>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>

#include "src/storage/lib/paver/pave-logging.h"

namespace paver {

namespace block = fuchsia_hardware_block;

zx::result<std::reference_wrapper<fuchsia_hardware_block::wire::BlockInfo>>
BlockPartitionClient::ReadBlockInfo() {
  if (block_info_.has_value()) {
    return zx::ok(std::reference_wrapper(block_info_.value()));
  }
  auto result = partition_->GetInfo();
  auto status = zx::make_result(result.ok() ? result.value().status : result.status());
  if (status.is_error()) {
    ERROR("Failed to get partition info with status: %s\n", status.status_string());
    return status.take_error();
  }
  return zx::ok(std::reference_wrapper(block_info_.emplace(*result.value().info)));
}

zx::result<size_t> BlockPartitionClient::GetBlockSize() {
  zx::result block_info = ReadBlockInfo();
  if (block_info.is_error()) {
    return block_info.take_error();
  }
  return zx::ok(block_info.value().get().block_size);
}

zx::result<size_t> BlockPartitionClient::GetPartitionSize() {
  zx::result block_info_result = ReadBlockInfo();
  if (block_info_result.is_error()) {
    return block_info_result.take_error();
  }
  const fuchsia_hardware_block::wire::BlockInfo& block_info = block_info_result.value().get();
  return zx::ok(block_info.block_size * block_info.block_count);
}

zx::result<> BlockPartitionClient::RegisterFastBlockIo() {
  if (client_) {
    return zx::ok();
  }
  zx::result endpoints = fidl::CreateEndpoints<block::Session>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto& [client, server] = endpoints.value();
  if (fidl::Status result = partition_->OpenSession(std::move(server)); !result.ok()) {
    return zx::error(result.status());
  }
  const fidl::WireResult result = fidl::WireCall(client)->GetFifo();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  fit::result response = result.value();
  if (response.is_error()) {
    return response.take_error();
  }
  client_ =
      std::make_unique<block_client::Client>(std::move(client), std::move(response.value()->fifo));
  return zx::ok();
}

zx::result<storage::Vmoid> BlockPartitionClient::Setup(const zx::vmo& vmo) {
  auto status = RegisterFastBlockIo();
  if (status.is_error()) {
    return status.take_error();
  }

  auto register_status = client_->RegisterVmo(vmo);
  if (register_status.is_error()) {
    return register_status.take_error();
  }

  return zx::ok(std::move(register_status.value()));
}

zx::result<> BlockPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  return Read(vmo, size, 0, 0);
}

zx::result<> BlockPartitionClient::Read(const zx::vmo& vmo, size_t size, size_t dev_offset,
                                        size_t vmo_offset) {
  zx::result block_size = GetBlockSize();
  if (block_size.is_error()) {
    return block_size.take_error();
  }
  const uint64_t length = size / block_size.value();
  if (length > UINT32_MAX) {
    ERROR("Error reading partition data: Too large\n");
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  zx::result vmoid = Setup(vmo);
  if (vmoid.is_error()) {
    return vmoid.take_error();
  }

  block_fifo_request_t request = {
      .opcode = BLOCKIO_READ,
      .group = 0,
      .vmoid = vmoid.value().TakeId(),
      .length = static_cast<uint32_t>(length),
      .vmo_offset = vmo_offset,
      .dev_offset = dev_offset,
  };

  if (auto status = zx::make_result(client_->Transaction(&request, 1)); status.is_error()) {
    ERROR("Error reading partition data: %s\n", status.status_string());
    return status.take_error();
  }

  return zx::ok();
}

zx::result<> BlockPartitionClient::Write(const zx::vmo& vmo, size_t vmo_size) {
  return Write(vmo, vmo_size, 0, 0);
}

zx::result<> BlockPartitionClient::Write(const zx::vmo& vmo, size_t vmo_size, size_t dev_offset,
                                         size_t vmo_offset) {
  zx::result block_size = GetBlockSize();
  if (block_size.is_error()) {
    return block_size.take_error();
  }
  uint64_t length = vmo_size / block_size.value();
  if (length > UINT32_MAX) {
    ERROR("Error writing partition data: Too large\n");
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  zx::result vmoid = Setup(vmo);
  if (vmoid.is_error()) {
    return vmoid.take_error();
  }

  block_fifo_request_t request = {
      .opcode = BLOCKIO_WRITE,
      .group = 0,
      .vmoid = vmoid.value().TakeId(),
      .length = static_cast<uint32_t>(length),
      .vmo_offset = vmo_offset,
      .dev_offset = dev_offset,
  };

  if (auto status = zx::make_result(client_->Transaction(&request, 1)); status.is_error()) {
    ERROR("Error writing partition data: %s\n", status.status_string());
    return status.take_error();
  }
  return zx::ok();
}

zx::result<> BlockPartitionClient::Trim() {
  zx::result block_info = ReadBlockInfo();
  if (block_info.is_error()) {
    return block_info.take_error();
  }
  uint64_t block_count = block_info.value().get().block_count;

  if (zx::result status = RegisterFastBlockIo(); status.is_error()) {
    return status.take_error();
  }

  block_fifo_request_t request = {
      .opcode = BLOCKIO_TRIM,
      .group = 0,
      .vmoid = BLOCK_VMOID_INVALID,
      .length = static_cast<uint32_t>(block_count),
      .vmo_offset = 0,
      .dev_offset = 0,
  };

  return zx::make_result(client_->Transaction(&request, 1));
}

zx::result<> BlockPartitionClient::Flush() {
  auto status = RegisterFastBlockIo();
  if (status.is_error()) {
    return status.take_error();
  }

  block_fifo_request_t request = {
      .opcode = BLOCKIO_FLUSH,
      .group = 0,
      .vmoid = BLOCK_VMOID_INVALID,
      .length = 0,
      .vmo_offset = 0,
      .dev_offset = 0,
  };

  return zx::make_result(client_->Transaction(&request, 1));
}

fidl::ClientEnd<block::Block> BlockPartitionClient::GetChannel() {
  return component::MaybeClone(partition_.client_end(), component::AssumeProtocolComposesNode);
}

fbl::unique_fd BlockPartitionClient::block_fd() {
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  zx::result cloned =
      component::Clone(partition_.client_end(), component::AssumeProtocolComposesNode);
  if (cloned.is_error()) {
    ERROR("Failed to clone partition client: %s\n", cloned.status_string());
    return {};
  }

  fbl::unique_fd fd;
  if (zx_status_t status =
          fdio_fd_create(cloned.value().TakeChannel().release(), fd.reset_and_get_address());
      status != ZX_OK) {
    ERROR("Failed to create block fd: %s\n", zx_status_get_string(status));
    return {};
  }
  return fd;
}

// The partition size does not account for the offset.
zx::result<size_t> FixedOffsetBlockPartitionClient::GetPartitionSize() {
  auto status_or_block_size = GetBlockSize();
  if (status_or_block_size.is_error()) {
    return status_or_block_size.take_error();
  }
  const size_t block_size = status_or_block_size.value();

  auto status_or_part_size = client_.GetPartitionSize();
  if (status_or_part_size.is_error()) {
    return status_or_part_size.take_error();
  }
  const size_t full_size = status_or_part_size.value();

  if (full_size < block_size * offset_partition_in_blocks_) {
    ERROR("Inconsistent partition size with block counts and block size\n");
    return zx::error(ZX_ERR_INTERNAL);
  }

  return zx::ok(full_size - block_size * offset_partition_in_blocks_);
}

zx::result<> FixedOffsetBlockPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  return client_.Read(vmo, size, offset_partition_in_blocks_, offset_buffer_in_blocks_);
}

zx::result<> FixedOffsetBlockPartitionClient::Write(const zx::vmo& vmo, size_t vmo_size) {
  return client_.Write(vmo, vmo_size, offset_partition_in_blocks_, offset_buffer_in_blocks_);
}

zx::result<size_t> FixedOffsetBlockPartitionClient::GetBufferOffsetInBytes() {
  auto status_or_block_size = GetBlockSize();
  if (status_or_block_size.is_error()) {
    return status_or_block_size.take_error();
  }
  const size_t block_size = status_or_block_size.value();
  return zx::ok(block_size * offset_buffer_in_blocks_);
}

zx::result<size_t> FixedOffsetBlockPartitionClient::GetBlockSize() {
  return client_.GetBlockSize();
}

zx::result<> FixedOffsetBlockPartitionClient::Trim() { return client_.Trim(); }

zx::result<> FixedOffsetBlockPartitionClient::Flush() { return client_.Flush(); }

fidl::ClientEnd<block::Block> FixedOffsetBlockPartitionClient::GetChannel() {
  return client_.GetChannel();
}

fbl::unique_fd FixedOffsetBlockPartitionClient::block_fd() { return client_.block_fd(); }

zx::result<size_t> PartitionCopyClient::GetBlockSize() {
  // Choose the lowest common multiple of all block sizes.
  size_t lcm = 1;
  for (auto& partition : partitions_) {
    if (auto status = partition->GetBlockSize(); status.is_ok()) {
      lcm = std::lcm(lcm, status.value());
    }
  }
  if (lcm == 0 || lcm == 1) {
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok(lcm);
}

zx::result<size_t> PartitionCopyClient::GetPartitionSize() {
  // Return minimum size of all partitions.
  bool one_succeed = false;
  size_t minimum_size = UINT64_MAX;
  for (auto& partition : partitions_) {
    if (auto status = partition->GetPartitionSize(); status.is_ok()) {
      one_succeed = true;
      minimum_size = std::min(minimum_size, status.value());
    }
  }
  if (!one_succeed) {
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok(minimum_size);
}

zx::result<> PartitionCopyClient::Read(const zx::vmo& vmo, size_t size) {
  // Read until one is successful.
  for (auto& partition : partitions_) {
    if (auto status = partition->Read(vmo, size); status.is_ok()) {
      return zx::ok();
    }
  }
  return zx::error(ZX_ERR_IO);
}

zx::result<> PartitionCopyClient::Write(const zx::vmo& vmo, size_t size) {
  // Guaranatee at least one write was successful.
  bool one_succeed = false;
  for (auto& partition : partitions_) {
    if (auto status = partition->Write(vmo, size); status.is_ok()) {
      one_succeed = true;
    } else {
      // Best effort trim the partition.
      partition->Trim().status_value();
    }
  }
  if (one_succeed) {
    return zx::ok();
  }
  return zx::error(ZX_ERR_IO);
}

zx::result<> PartitionCopyClient::Trim() {
  // All must trim successfully.
  for (auto& partition : partitions_) {
    if (auto status = partition->Trim(); status.is_error()) {
      return status.take_error();
    }
  }
  return zx::ok();
}

zx::result<> PartitionCopyClient::Flush() {
  // All must flush successfully.
  for (auto& partition : partitions_) {
    if (auto status = partition->Flush(); status.is_error()) {
      return status.take_error();
    }
  }
  return zx::ok();
}

fidl::ClientEnd<block::Block> PartitionCopyClient::GetChannel() { return {}; }

fbl::unique_fd PartitionCopyClient::block_fd() { return fbl::unique_fd(); }

}  // namespace paver
