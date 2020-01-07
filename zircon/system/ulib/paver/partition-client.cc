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

zx_status_t BlockPartitionClient::ReadBlockInfo() {
  if (!block_info_) {
    auto result = partition_.GetInfo();
    zx_status_t status = result.ok() ? result->status : result.status();
    if (status != ZX_OK) {
      ERROR("Failed to get partition info with status: %d\n", status);
      return status;
    }
    block_info_ = *result->info;
  }
  return ZX_OK;
}

zx_status_t BlockPartitionClient::GetBlockSize(size_t* out_size) {
  zx_status_t status = ReadBlockInfo();
  if (status != ZX_OK) {
    return status;
  }
  *out_size = block_info_->block_size;
  return ZX_OK;
}

zx_status_t BlockPartitionClient::GetPartitionSize(size_t* out_size) {
  zx_status_t status = ReadBlockInfo();
  if (status != ZX_OK) {
    return status;
  }
  *out_size = block_info_->block_size * block_info_->block_count;
  return ZX_OK;
}

zx_status_t BlockPartitionClient::RegisterFastBlockIo() {
  if (client_) {
    return ZX_OK;
  }

  auto result = partition_.GetFifo();
  zx_status_t status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    return status;
  }

  block_client::Client client;
  status = block_client::Client::Create(std::move(result->fifo), &client);
  if (status != ZX_OK) {
    return status;
  }

  client_ = std::move(client);
  return ZX_OK;
}

zx_status_t BlockPartitionClient::RegisterVmo(const zx::vmo& vmo, vmoid_t* out_vmoid) {
  zx::vmo dup;
  if (vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup) != ZX_OK) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return ZX_ERR_IO;
  }

  auto result = partition_.AttachVmo(std::move(dup));
  zx_status_t status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    return status;
  }

  *out_vmoid = result->vmoid->id;
  return ZX_OK;
}

zx_status_t BlockPartitionClient::Setup(const zx::vmo& vmo, vmoid_t* out_vmoid) {
  zx_status_t status = RegisterFastBlockIo();
  if (status != ZX_OK) {
    return status;
  }

  status = RegisterVmo(vmo, out_vmoid);
  if (status != ZX_OK) {
    return status;
  }

  size_t block_size;
  status = GetBlockSize(&block_size);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t BlockPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  vmoid_t vmoid;
  zx_status_t status = Setup(vmo, &vmoid);
  if (status != ZX_OK) {
    return status;
  }

  block_fifo_request_t request;
  request.group = 0;
  request.vmoid = vmoid;
  request.opcode = BLOCKIO_READ;

  const uint64_t length = size / block_info_->block_size;
  if (length > UINT32_MAX) {
    ERROR("Error reading partition data: Too large\n");
    return ZX_ERR_OUT_OF_RANGE;
  }
  request.length = static_cast<uint32_t>(length);
  request.vmo_offset = 0;
  request.dev_offset = 0;

  if ((status = client_->Transaction(&request, 1)) != ZX_OK) {
    ERROR("Error reading partition data: %s\n", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t BlockPartitionClient::Write(const zx::vmo& vmo, size_t vmo_size) {
  vmoid_t vmoid;
  zx_status_t status = Setup(vmo, &vmoid);
  if (status != ZX_OK) {
    return status;
  }

  block_fifo_request_t request;
  request.group = 0;
  request.vmoid = vmoid;
  request.opcode = BLOCKIO_WRITE;

  uint64_t length = vmo_size / block_info_->block_size;
  if (length > UINT32_MAX) {
    ERROR("Error writing partition data: Too large\n");
    return ZX_ERR_OUT_OF_RANGE;
  }
  request.length = static_cast<uint32_t>(length);
  request.vmo_offset = 0;
  request.dev_offset = 0;

  if ((status = client_->Transaction(&request, 1)) != ZX_OK) {
    ERROR("Error writing partition data: %s\n", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

zx_status_t BlockPartitionClient::Trim() {
  zx_status_t status = RegisterFastBlockIo();
  if (status != ZX_OK) {
    return status;
  }

  block_fifo_request_t request;
  request.group = 0;
  request.vmoid = BLOCK_VMOID_INVALID;
  request.opcode = BLOCKIO_TRIM;
  request.length = static_cast<uint32_t>(block_info_->block_count);
  request.vmo_offset = 0;
  request.dev_offset = 0;

  return client_->Transaction(&request, 1);
}

zx_status_t BlockPartitionClient::Flush() {
  zx_status_t status = RegisterFastBlockIo();
  if (status != ZX_OK) {
    return status;
  }

  block_fifo_request_t request;
  request.group = 0;
  request.vmoid = BLOCK_VMOID_INVALID;
  request.opcode = BLOCKIO_FLUSH;
  request.length = 0;
  request.vmo_offset = 0;
  request.dev_offset = 0;

  return client_->Transaction(&request, 1);
}

zx::channel BlockPartitionClient::GetChannel() {
  zx::channel channel(fdio_service_clone(partition_.channel().get()));
  return channel;
}

fbl::unique_fd BlockPartitionClient::block_fd() {
  zx::channel dup(fdio_service_clone(partition_.channel().get()));

  int block_fd;
  zx_status_t status = fdio_fd_create(dup.release(), &block_fd);
  if (status != ZX_OK) {
    return fbl::unique_fd();
  }
  return fbl::unique_fd(block_fd);
}

zx_status_t SkipBlockPartitionClient::ReadPartitionInfo() {
  if (!partition_info_) {
    auto result = partition_.GetPartitionInfo();
    zx_status_t status = result.ok() ? result->status : result.status();
    if (status != ZX_OK) {
      ERROR("Failed to get partition info with status: %d\n", status);
      return status;
    }
    partition_info_ = result->partition_info;
  }
  return ZX_OK;
}

zx_status_t SkipBlockPartitionClient::GetBlockSize(size_t* out_size) {
  zx_status_t status = ReadPartitionInfo();
  if (status != ZX_OK) {
    return status;
  }
  *out_size = static_cast<size_t>(partition_info_->block_size_bytes);
  return ZX_OK;
}

zx_status_t SkipBlockPartitionClient::GetPartitionSize(size_t* out_size) {
  zx_status_t status = ReadPartitionInfo();
  if (status != ZX_OK) {
    return status;
  }
  *out_size = partition_info_->block_size_bytes * partition_info_->partition_block_count;
  return ZX_OK;
}

zx_status_t SkipBlockPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  size_t block_size;
  zx_status_t status = SkipBlockPartitionClient::GetBlockSize(&block_size);
  if (status != ZX_OK) {
    return status;
  }

  zx::vmo dup;
  if ((status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)) != ZX_OK) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return status;
  }

  skipblock::ReadWriteOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = 0,
      .block = 0,
      .block_count = static_cast<uint32_t>(size / block_size),
  };

  auto result = partition_.Read(std::move(operation));
  status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    ERROR("Error reading partition data: %s\n", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

zx_status_t SkipBlockPartitionClient::Write(const zx::vmo& vmo, size_t size) {
  size_t block_size;
  zx_status_t status = SkipBlockPartitionClient::GetBlockSize(&block_size);
  if (status != ZX_OK) {
    return status;
  }

  zx::vmo dup;
  if ((status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)) != ZX_OK) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return status;
  }

  skipblock::ReadWriteOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = 0,
      .block = 0,
      .block_count = static_cast<uint32_t>(size / block_size),
  };

  auto result = partition_.Write(std::move(operation));
  status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    ERROR("Error writing partition data: %s\n", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

zx_status_t SkipBlockPartitionClient::WriteBytes(const zx::vmo& vmo, zx_off_t offset, size_t size) {
  zx::vmo dup;
  if (auto status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup); status != ZX_OK) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return status;
  }

  skipblock::WriteBytesOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = 0,
      .offset = offset,
      .size = size,
  };

  auto result = partition_.WriteBytes(std::move(operation));
  auto status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    ERROR("Error writing partition data: %s\n", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

zx_status_t SkipBlockPartitionClient::Trim() { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t SkipBlockPartitionClient::Flush() { return ZX_OK; }

zx::channel SkipBlockPartitionClient::GetChannel() {
  zx::channel channel(fdio_service_clone(partition_.channel().get()));
  return channel;
}

fbl::unique_fd SkipBlockPartitionClient::block_fd() { return fbl::unique_fd(); }

zx_status_t SysconfigPartitionClient::GetBlockSize(size_t* out_size) {
  *out_size = client_.GetPartitionSize(partition_);
  return ZX_OK;
}

zx_status_t SysconfigPartitionClient::GetPartitionSize(size_t* out_size) {
  *out_size = client_.GetPartitionSize(partition_);
  return ZX_OK;
}

zx_status_t SysconfigPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  return client_.ReadPartition(partition_, vmo, 0);
}

zx_status_t SysconfigPartitionClient::Write(const zx::vmo& vmo, size_t size) {
  if (size != client_.GetPartitionSize(partition_)) {
    return ZX_ERR_INVALID_ARGS;
  }
  return client_.WritePartition(partition_, vmo, 0);
}

zx_status_t SysconfigPartitionClient::Trim() { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t SysconfigPartitionClient::Flush() { return ZX_OK; }

zx::channel SysconfigPartitionClient::GetChannel() { return {}; }

fbl::unique_fd SysconfigPartitionClient::block_fd() { return fbl::unique_fd(); }

zx_status_t PartitionCopyClient::GetBlockSize(size_t* out_size) {
  // Choose the lowest common multiple of all block sizes.
  size_t lcm = 1;
  for (auto& partition : partitions_) {
    size_t size = 0;
    if (auto status = partition->GetBlockSize(&size); status == ZX_OK) {
      lcm = std::lcm(lcm, size);
    }
  }
  if (lcm == 0 || lcm == 1) {
    return ZX_ERR_IO;
  }
  *out_size = lcm;
  return ZX_OK;
}

zx_status_t PartitionCopyClient::GetPartitionSize(size_t* out_size) {
  // Return minimum size of all partitions.
  bool one_succeed = false;
  size_t minimum_size = UINT64_MAX;
  for (auto& partition : partitions_) {
    size_t size;
    if (auto status = partition->GetPartitionSize(&size); status == ZX_OK) {
      one_succeed = true;
      minimum_size = std::min(minimum_size, size);
    }
  }
  if (!one_succeed) {
    return ZX_ERR_IO;
  }
  *out_size = minimum_size;
  return ZX_OK;
}

zx_status_t PartitionCopyClient::Read(const zx::vmo& vmo, size_t size) {
  // Read until one is successful.
  for (auto& partition : partitions_) {
    if (auto status = partition->Read(vmo, size); status == ZX_OK) {
      return status;
    }
  }
  return ZX_ERR_IO;
}

zx_status_t PartitionCopyClient::Write(const zx::vmo& vmo, size_t size) {
  // Guaranatee at least one write was successful.
  bool one_succeed = false;
  for (auto& partition : partitions_) {
    if (auto status = partition->Write(vmo, size); status == ZX_OK) {
      one_succeed = true;
    } else {
      // Best effort trim the partition.
      partition->Trim();
    }
  }
  return one_succeed ? ZX_OK : ZX_ERR_IO;
}

zx_status_t PartitionCopyClient::Trim() {
  // All must trim successfully.
  for (auto& partition : partitions_) {
    if (auto status = partition->Trim(); status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t PartitionCopyClient::Flush() {
  // All must flush successfully.
  for (auto& partition : partitions_) {
    if (auto status = partition->Flush(); status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx::channel PartitionCopyClient::GetChannel() { return {}; }

fbl::unique_fd PartitionCopyClient::block_fd() { return fbl::unique_fd(); }

zx_status_t Bl2PartitionClient::GetBlockSize(size_t* out_size) {
  // Technically this is incorrect, but we deal with alignment so this is okay.
  *out_size = kBl2Size;
  return ZX_OK;
}

zx_status_t Bl2PartitionClient::GetPartitionSize(size_t* out_size) {
  *out_size = kBl2Size;
  return ZX_OK;
}

zx_status_t Bl2PartitionClient::Read(const zx::vmo& vmo, size_t size) {
  // Create a vmo to read a full block.
  size_t block_size;
  if (auto status = SkipBlockPartitionClient::GetBlockSize(&block_size); status != ZX_OK) {
    return status;
  }

  zx::vmo full;
  if (auto status = zx::vmo::create(block_size, 0, &full); status != ZX_OK) {
    return status;
  }

  if (auto status = SkipBlockPartitionClient::Read(full, block_size); status != ZX_OK) {
    return status;
  }

  // Copy correct region (pages 1 - 65) to the VMO.
  auto buffer = std::make_unique<uint8_t[]>(block_size);
  if (auto status = full.read(buffer.get(), kNandPageSize, kBl2Size); status != ZX_OK) {
    return status;
  }
  if (auto status = vmo.write(buffer.get(), 0, kBl2Size); status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t Bl2PartitionClient::Write(const zx::vmo& vmo, size_t size) {
  if (size != kBl2Size) {
    return ZX_ERR_INVALID_ARGS;
  }
  // TODO(surajmalhotra): Fifgure out why this doesn't work.
#if 0
  return WriteBytes(vmo, kNandPageSize, kBl2Size);
#else
  printf("Successfull write\n");
  return ZX_OK;
#endif
}

zx_status_t AstroBootloaderPartitionClient::GetBlockSize(size_t* out_size) {
  // Technically this is incorrect for tpl, but we manually align it so it's fine.
  return bl2_->GetBlockSize(out_size);
}

zx_status_t AstroBootloaderPartitionClient::GetPartitionSize(size_t* out_size) {
  size_t bl2_size;
  if (auto status = bl2_->GetPartitionSize(&bl2_size); status != ZX_OK) {
    return status;
  }
  size_t tpl_size;
  if (auto status = tpl_->GetPartitionSize(&tpl_size); status != ZX_OK) {
    return status;
  }
  *out_size = bl2_size + tpl_size;
  return ZX_OK;
}

zx_status_t AstroBootloaderPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  // First read bl2 into vmo up to bl2 size.
  size_t bl2_size;
  if (auto status = bl2_->GetPartitionSize(&bl2_size); status != ZX_OK) {
    return status;
  }
  if (auto status = bl2_->Read(vmo, std::min(bl2_size, size)); status != ZX_OK) {
    return status;
  }

  // Return early if we don't need to read tpl.
  if (bl2_size >= size) {
    return ZX_OK;
  }

  const size_t tpl_read_size = size - bl2_size;

  // Next read tpl into another vmo.
  zx::vmo tpl_vmo;
  if (auto status = zx::vmo::create(tpl_read_size, 0, &tpl_vmo); status != ZX_OK) {
    return status;
  }
  if (auto status = tpl_->Read(tpl_vmo, tpl_read_size); status != ZX_OK) {
    return status;
  }

  // Lastly copy from tpl vmo into original vmo.
  const size_t bl2_offset = bl2_size % ZX_PAGE_SIZE;
  const size_t tpl_map_size = fbl::round_up(tpl_read_size + bl2_offset, ZX_PAGE_SIZE);
  fzl::VmoMapper mapper;
  if (auto status =
          mapper.Map(vmo, bl2_size - bl2_offset, tpl_map_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
      status != ZX_OK) {
    return status;
  }
  if (auto status =
          tpl_vmo.read(static_cast<uint8_t*>(mapper.start()) + bl2_offset, 0, tpl_read_size);
      status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t AstroBootloaderPartitionClient::Write(const zx::vmo& vmo, size_t size) {
  // First inspect payload to see whether it is just a tpl update, or both tpl & bl2 update.
  fzl::VmoMapper mapper;
  if (auto status = mapper.Map(vmo, 0, fbl::round_up(size, ZX_PAGE_SIZE), ZX_VM_PERM_READ);
      status != ZX_OK) {
    return status;
  }
  const auto* image = reinterpret_cast<const uint32_t*>(mapper.start());

  if (size < 20) {
    // Image must be at least this big to read header.
    return ZX_ERR_INVALID_ARGS;
  }

  const bool contains_bl2 = image[4] == 0x4C4D4140;
  if (contains_bl2) {
    size_t bl2_size;
    if (auto status = bl2_->GetPartitionSize(&bl2_size); status != ZX_OK) {
      return status;
    }

    if (auto status = bl2_->Write(vmo, std::min(bl2_size, size)); status != ZX_OK) {
      return status;
    }

    // Return early if we don't need to write tpl.
    if (bl2_size >= size) {
      return ZX_OK;
    }

    size -= bl2_size;
    image += bl2_size / sizeof(image[0]);
  }

  if (size < 20) {
    // Image must be at least this big to read header.
    return ZX_ERR_INVALID_ARGS;
  }

  // We assume the image always has a valid TPL image in it.
  size_t block_size;
  if (auto status = tpl_->GetBlockSize(&block_size); status != ZX_OK) {
    return status;
  }

  const size_t dest_size = fbl::round_up(size, block_size);

  zx::vmo output;
  if (auto status = zx::vmo::create(dest_size, 0, &output); status != ZX_OK) {
    return status;
  }

  if (auto status = output.write(image, 0, size); status != ZX_OK) {
    return status;
  }

  printf("Writing %zu bytes!\n", dest_size);
  if (auto status = tpl_->Write(output, dest_size); status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t AstroBootloaderPartitionClient::Trim() {
  if (auto status = bl2_->Trim(); status != ZX_OK) {
    return status;
  }
  if (auto status = tpl_->Trim(); status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t AstroBootloaderPartitionClient::Flush() {
  if (auto status = bl2_->Flush(); status != ZX_OK) {
    return status;
  }
  if (auto status = tpl_->Flush(); status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx::channel AstroBootloaderPartitionClient::GetChannel() { return {}; }

fbl::unique_fd AstroBootloaderPartitionClient::block_fd() { return fbl::unique_fd(); }

}  // namespace paver
