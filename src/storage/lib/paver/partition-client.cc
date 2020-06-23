// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "partition-client.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/limits.h>
#include <zircon/status.h>

#include <cstdint>
#include <numeric>

#include <fbl/algorithm.h>

#include "pave-logging.h"
#include "zircon/errors.h"

namespace paver {
namespace {

namespace block = ::llcpp::fuchsia::hardware::block;
namespace skipblock = ::llcpp::fuchsia::hardware::skipblock;

}  // namespace

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

zx::status<> SkipBlockPartitionClient::ReadPartitionInfo() {
  if (!partition_info_) {
    auto result = partition_.GetPartitionInfo();
    auto status = zx::make_status(result.ok() ? result->status : result.status());
    if (status.is_error()) {
      ERROR("Failed to get partition info with status: %s\n", status.status_string());
      return status.take_error();
    }
    partition_info_ = result->partition_info;
  }
  return zx::ok();
}

zx::status<size_t> SkipBlockPartitionClient::GetBlockSize() {
  auto status = ReadPartitionInfo();
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(static_cast<size_t>(partition_info_->block_size_bytes));
}

zx::status<size_t> SkipBlockPartitionClient::GetPartitionSize() {
  auto status = ReadPartitionInfo();
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(partition_info_->block_size_bytes * partition_info_->partition_block_count);
}

zx::status<> SkipBlockPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  auto status = SkipBlockPartitionClient::GetBlockSize();
  if (status.is_error()) {
    return status.take_error();
  }
  const size_t block_size = status.value();

  zx::vmo dup;
  if (auto status = zx::make_status(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)); status.is_error()) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return status.take_error();
  }

  skipblock::ReadWriteOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = 0,
      .block = 0,
      .block_count = static_cast<uint32_t>(size / block_size),
  };

  auto result = partition_.Read(std::move(operation));
  {
    auto status = zx::make_status(result.ok() ? result->status : result.status());
    if (status.is_error()) {
      ERROR("Error reading partition data: %s\n", status.status_string());
      return status.take_error();
    }
  }
  return zx::ok();
}

zx::status<> SkipBlockPartitionClient::Write(const zx::vmo& vmo, size_t size) {
  auto status = SkipBlockPartitionClient::GetBlockSize();
  if (status.is_error()) {
    return status.take_error();
  }
  size_t block_size = status.value();

  zx::vmo dup;
  if (auto status = zx::make_status(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)); status.is_error()) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return status.take_error();
  }

  skipblock::ReadWriteOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = 0,
      .block = 0,
      .block_count = static_cast<uint32_t>(size / block_size),
  };

  auto result = partition_.Write(std::move(operation));
  {
    auto status = zx::make_status(result.ok() ? result->status : result.status());
    if (status.is_error()) {
      ERROR("Error writing partition data: %s\n", status.status_string());
      return status.take_error();
    }
  }
  return zx::ok();
}

zx::status<> SkipBlockPartitionClient::WriteBytes(const zx::vmo& vmo, zx_off_t offset,
                                                  size_t size) {
  zx::vmo dup;
  if (auto status = zx::make_status(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)); status.is_error()) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return status.take_error();
  }

  skipblock::WriteBytesOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = 0,
      .offset = offset,
      .size = size,
      .mode = skipblock::WriteBytesMode::READ_MODIFY_ERASE_WRITE,
  };

  auto result = partition_.WriteBytes(std::move(operation));
  auto status = zx::make_status(result.ok() ? result->status : result.status());
  if (status.is_error()) {
    ERROR("Error writing partition data: %s\n", status.status_string());
    return status.take_error();
  }
  return zx::ok();
}

zx::status<> SkipBlockPartitionClient::Trim() { return zx::error(ZX_ERR_NOT_SUPPORTED); }

zx::status<> SkipBlockPartitionClient::Flush() { return zx::ok(); }

zx::channel SkipBlockPartitionClient::GetChannel() {
  zx::channel channel(fdio_service_clone(partition_.channel().get()));
  return channel;
}

fbl::unique_fd SkipBlockPartitionClient::block_fd() { return fbl::unique_fd(); }

zx::status<size_t> SysconfigPartitionClient::GetBlockSize() {
  size_t size;
  auto status = zx::make_status(client_.GetPartitionSize(partition_, &size));
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(size);
}

zx::status<size_t> SysconfigPartitionClient::GetPartitionSize() {
  size_t size;
  auto status = zx::make_status(client_.GetPartitionSize(partition_, &size));
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(size);
}

zx::status<> SysconfigPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  return zx::make_status(client_.ReadPartition(partition_, vmo, 0));
}

zx::status<> SysconfigPartitionClient::Write(const zx::vmo& vmo, size_t size) {
  size_t partition_size;
  if (auto status = client_.GetPartitionSize(partition_, &partition_size); status != ZX_OK) {
    return zx::error(status);
  }

  if (size != partition_size) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return zx::make_status(client_.WritePartition(partition_, vmo, 0));
}

zx::status<> SysconfigPartitionClient::Trim() { return zx::error(ZX_ERR_NOT_SUPPORTED); }

zx::status<> SysconfigPartitionClient::Flush() { return zx::ok(); }

zx::channel SysconfigPartitionClient::GetChannel() { return {}; }

fbl::unique_fd SysconfigPartitionClient::block_fd() { return fbl::unique_fd(); }

zx::status<size_t> AstroSysconfigPartitionClientBuffered::GetBlockSize() {
  return context_->Call<AstroPartitionerContext, size_t>([&](auto* ctx) -> zx::status<size_t> {
    size_t size;
    auto status = zx::make_status(ctx->client_->GetPartitionSize(partition_, &size));
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(size);
  });
}

zx::status<size_t> AstroSysconfigPartitionClientBuffered::GetPartitionSize() {
  return context_->Call<AstroPartitionerContext, size_t>([&](auto* ctx) -> zx::status<size_t> {
    size_t size;
    auto status = zx::make_status(ctx->client_->GetPartitionSize(partition_, &size));
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(size);
  });
}

zx::status<> AstroSysconfigPartitionClientBuffered::Read(const zx::vmo& vmo, size_t size) {
  return context_->Call<AstroPartitionerContext>(
      [&](auto* ctx) { return zx::make_status(ctx->client_->ReadPartition(partition_, vmo, 0)); });
}

zx::status<> AstroSysconfigPartitionClientBuffered::Write(const zx::vmo& vmo, size_t size) {
  return context_->Call<AstroPartitionerContext>([&](auto* ctx) -> zx::status<> {
    size_t partition_size;
    auto status = zx::make_status(ctx->client_->GetPartitionSize(partition_, &partition_size));
    if (status.is_error()) {
      return status.take_error();
    }
    if (size != partition_size) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    return zx::make_status(ctx->client_->WritePartition(partition_, vmo, 0));
  });
}

zx::status<> AstroSysconfigPartitionClientBuffered::Trim() {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> AstroSysconfigPartitionClientBuffered::Flush() {
  return context_->Call<AstroPartitionerContext>(
      [&](auto* ctx) { return zx::make_status(ctx->client_->Flush()); });
}

zx::channel AstroSysconfigPartitionClientBuffered::GetChannel() { return {}; }

fbl::unique_fd AstroSysconfigPartitionClientBuffered::block_fd() { return fbl::unique_fd(); }

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

zx::status<size_t> Bl2PartitionClient::GetBlockSize() {
  // Technically this is incorrect, but we deal with alignment so this is okay.
  return zx::ok(kBl2Size);
}

zx::status<size_t> Bl2PartitionClient::GetPartitionSize() { return zx::ok(kBl2Size); }

zx::status<> Bl2PartitionClient::Read(const zx::vmo& vmo, size_t size) {
  // Create a vmo to read a full block.
  auto status = SkipBlockPartitionClient::GetBlockSize();
  if (status.is_error()) {
    return status.take_error();
  }
  const size_t block_size = status.value();

  zx::vmo full;
  if (auto status = zx::make_status(zx::vmo::create(block_size, 0, &full)); status.is_error()) {
    return status.take_error();
  }

  if (auto status = SkipBlockPartitionClient::Read(full, block_size); status.is_error()) {
    return status.take_error();
  }

  // Copy correct region (pages 1 - 65) to the VMO.
  auto buffer = std::make_unique<uint8_t[]>(block_size);
  if (auto status = zx::make_status(full.read(buffer.get(), kNandPageSize, kBl2Size));
      status.is_error()) {
    return status.take_error();
  }
  if (auto status = zx::make_status(vmo.write(buffer.get(), 0, kBl2Size)); status.is_error()) {
    return status.take_error();
  }

  return zx::ok();
}

zx::status<> Bl2PartitionClient::Write(const zx::vmo& vmo, size_t size) {
  if (size != kBl2Size) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return WriteBytes(vmo, kNandPageSize, kBl2Size);
}

zx::status<size_t> SherlockBootloaderPartitionClient::GetBlockSize() {
  return client_.GetBlockSize();
}

// Sherlock bootloader partition starts with one block of metadata used only
// by the firmware, our read/write/size functions should skip it.
zx::status<size_t> SherlockBootloaderPartitionClient::GetPartitionSize() {
  auto status_or_block_size = GetBlockSize();
  if (status_or_block_size.is_error()) {
    return status_or_block_size.take_error();
  }
  const size_t block_size = status_or_block_size.value();

  auto status_or_part_size = client_.GetPartitionSize();
  if (status_or_part_size.is_error()) {
    return status_or_part_size.take_error();
  }
  const size_t full_size = status_or_block_size.value();

  return zx::ok(full_size - block_size);
}

zx::status<> SherlockBootloaderPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  return client_.Read(vmo, size, 1);
}

zx::status<> SherlockBootloaderPartitionClient::Write(const zx::vmo& vmo, size_t vmo_size) {
  return client_.Write(vmo, vmo_size, 1);
}

zx::status<> SherlockBootloaderPartitionClient::Trim() { return client_.Trim(); }

zx::status<> SherlockBootloaderPartitionClient::Flush() { return client_.Flush(); }

zx::channel SherlockBootloaderPartitionClient::GetChannel() { return client_.GetChannel(); }

fbl::unique_fd SherlockBootloaderPartitionClient::block_fd() { return client_.block_fd(); }

}  // namespace paver
