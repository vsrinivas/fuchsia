// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/partition-client.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/status.h>

#include <cstdint>
#include <numeric>

#include <fbl/algorithm.h>

#include "src/storage/lib/paver/pave-logging.h"

namespace paver {
namespace {

namespace block = ::llcpp::fuchsia::hardware::block;

}  // namespace

BlockPartitionClient::~BlockPartitionClient() {
  if (client_) {
    partition_.CloseFifo();
  }
}

zx::status<> BlockPartitionClient::ReadBlockInfo() {
  if (!block_info_) {
    auto result = partition_.GetInfo();
    auto status = zx::make_status(result.ok() ? result->status : result.status());
    if (status.is_error()) {
      ERROR("Failed to get partition info with status: %s\n", status.status_string());
      return status.take_error();
    }
    block_info_ = *result->info;
  }
  return zx::ok();
}

zx::status<size_t> BlockPartitionClient::GetBlockSize() {
  auto status = ReadBlockInfo();
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(block_info_->block_size);
}

zx::status<size_t> BlockPartitionClient::GetPartitionSize() {
  auto status = ReadBlockInfo();
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(block_info_->block_size * block_info_->block_count);
}

zx::status<> BlockPartitionClient::RegisterFastBlockIo() {
  if (client_) {
    return zx::ok();
  }

  auto result = partition_.GetFifo();
  auto status = zx::make_status(result.ok() ? result->status : result.status());
  if (status.is_error()) {
    return status.take_error();
  }

  block_client::Client client;
  status = zx::make_status(block_client::Client::Create(std::move(result->fifo), &client));
  if (status.is_error()) {
    return status.take_error();
  }

  client_ = std::move(client);
  return zx::ok();
}

zx::status<vmoid_t> BlockPartitionClient::RegisterVmo(const zx::vmo& vmo) {
  zx::vmo dup;
  if (vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup) != ZX_OK) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return zx::error(ZX_ERR_IO);
  }

  auto result = partition_.AttachVmo(std::move(dup));
  auto status = zx::make_status(result.ok() ? result->status : result.status());
  if (status.is_error()) {
    return status.take_error();
  }

  return zx::ok(result->vmoid->id);
}

zx::status<vmoid_t> BlockPartitionClient::Setup(const zx::vmo& vmo) {
  auto status = RegisterFastBlockIo();
  if (status.is_error()) {
    return status.take_error();
  }

  auto register_status = RegisterVmo(vmo);
  if (register_status.is_error()) {
    return register_status.take_error();
  }

  {
    auto status = GetBlockSize();
    if (status.is_error()) {
      return status.take_error();
    }
  }

  return zx::ok(register_status.value());
}

zx::status<> BlockPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  return Read(vmo, size, 0);
}

zx::status<> BlockPartitionClient::Read(const zx::vmo& vmo, size_t size, size_t dev_offset) {
  auto status = Setup(vmo);
  if (status.is_error()) {
    return status.take_error();
  }
  vmoid_t vmoid = status.value();

  block_fifo_request_t request;
  request.group = 0;
  request.vmoid = vmoid;
  request.opcode = BLOCKIO_READ;

  const uint64_t length = size / block_info_->block_size;
  if (length > UINT32_MAX) {
    ERROR("Error reading partition data: Too large\n");
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  request.length = static_cast<uint32_t>(length);
  request.vmo_offset = 0;
  request.dev_offset = dev_offset;

  if (auto status = zx::make_status(client_->Transaction(&request, 1)); status.is_error()) {
    ERROR("Error reading partition data: %s\n", status.status_string());
    return status.take_error();
  }

  return zx::ok();
}

zx::status<> BlockPartitionClient::Write(const zx::vmo& vmo, size_t vmo_size) {
  return Write(vmo, vmo_size, 0);
}

zx::status<> BlockPartitionClient::Write(const zx::vmo& vmo, size_t vmo_size, size_t dev_offset) {
  auto status = Setup(vmo);
  if (status.is_error()) {
    return status.take_error();
  }

  block_fifo_request_t request;
  request.group = 0;
  request.vmoid = status.value();
  request.opcode = BLOCKIO_WRITE;

  uint64_t length = vmo_size / block_info_->block_size;
  if (length > UINT32_MAX) {
    ERROR("Error writing partition data: Too large\n");
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  request.length = static_cast<uint32_t>(length);
  request.vmo_offset = 0;
  request.dev_offset = dev_offset;

  if (auto status = zx::make_status(client_->Transaction(&request, 1)); status.is_error()) {
    ERROR("Error writing partition data: %s\n", status.status_string());
    return status.take_error();
  }
  return zx::ok();
}

zx::status<> BlockPartitionClient::Trim() {
  auto status = RegisterFastBlockIo();
  if (status.is_error()) {
    return status.take_error();
  }

  block_fifo_request_t request;
  request.group = 0;
  request.vmoid = BLOCK_VMOID_INVALID;
  request.opcode = BLOCKIO_TRIM;
  request.length = static_cast<uint32_t>(block_info_->block_count);
  request.vmo_offset = 0;
  request.dev_offset = 0;

  return zx::make_status(client_->Transaction(&request, 1));
}

zx::status<> BlockPartitionClient::Flush() {
  auto status = RegisterFastBlockIo();
  if (status.is_error()) {
    return status.take_error();
  }

  block_fifo_request_t request;
  request.group = 0;
  request.vmoid = BLOCK_VMOID_INVALID;
  request.opcode = BLOCKIO_FLUSH;
  request.length = 0;
  request.vmo_offset = 0;
  request.dev_offset = 0;

  return zx::make_status(client_->Transaction(&request, 1));
}

zx::channel BlockPartitionClient::GetChannel() {
  zx::channel channel(fdio_service_clone(partition_.channel().get()));
  return channel;
}

fbl::unique_fd BlockPartitionClient::block_fd() {
  zx::channel dup(fdio_service_clone(partition_.channel().get()));

  int block_fd;
  auto status = zx::make_status(fdio_fd_create(dup.release(), &block_fd));
  if (status.is_error()) {
    return fbl::unique_fd();
  }
  return fbl::unique_fd(block_fd);
}

// The partition size does not account for the offset.
zx::status<size_t> FixedOffsetBlockPartitionClient::GetPartitionSize() {
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

  if (full_size < block_size * offset_in_blocks_) {
    ERROR("Inconsistent partition size with block counts and block size\n");
    return zx::error(ZX_ERR_INTERNAL);
  }

  return zx::ok(full_size - block_size * offset_in_blocks_);
}

zx::status<> FixedOffsetBlockPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  return client_.Read(vmo, size, offset_in_blocks_);
}

zx::status<> FixedOffsetBlockPartitionClient::Write(const zx::vmo& vmo, size_t vmo_size) {
  return client_.Write(vmo, vmo_size, offset_in_blocks_);
}

zx::status<size_t> FixedOffsetBlockPartitionClient::GetBlockSize() { return client_.GetBlockSize(); }

zx::status<> FixedOffsetBlockPartitionClient::Trim() { return client_.Trim(); }

zx::status<> FixedOffsetBlockPartitionClient::Flush() { return client_.Flush(); }

zx::channel FixedOffsetBlockPartitionClient::GetChannel() { return client_.GetChannel(); }

fbl::unique_fd FixedOffsetBlockPartitionClient::block_fd() { return client_.block_fd(); }

zx::status<size_t> PartitionCopyClient::GetBlockSize() {
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

zx::status<size_t> PartitionCopyClient::GetPartitionSize() {
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

zx::status<> PartitionCopyClient::Read(const zx::vmo& vmo, size_t size) {
  // Read until one is successful.
  for (auto& partition : partitions_) {
    if (auto status = partition->Read(vmo, size); status.is_ok()) {
      return zx::ok();
    }
  }
  return zx::error(ZX_ERR_IO);
}

zx::status<> PartitionCopyClient::Write(const zx::vmo& vmo, size_t size) {
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
  } else {
    return zx::error(ZX_ERR_IO);
  }
}

zx::status<> PartitionCopyClient::Trim() {
  // All must trim successfully.
  for (auto& partition : partitions_) {
    if (auto status = partition->Trim(); status.is_error()) {
      return status.take_error();
    }
  }
  return zx::ok();
}

zx::status<> PartitionCopyClient::Flush() {
  // All must flush successfully.
  for (auto& partition : partitions_) {
    if (auto status = partition->Flush(); status.is_error()) {
      return status.take_error();
    }
  }
  return zx::ok();
}

zx::channel PartitionCopyClient::GetChannel() { return {}; }

fbl::unique_fd PartitionCopyClient::block_fd() { return fbl::unique_fd(); }

}  // namespace paver
