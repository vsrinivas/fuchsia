// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/skip-block.h"

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <libgen.h>

#include <fbl/string_buffer.h>
#include <gpt/gpt.h>

#include "src/lib/uuid/uuid.h"
#include "src/storage/lib/paver/fvm.h"
#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/utils.h"

namespace paver {

namespace {

using uuid::Uuid;

namespace block = ::llcpp::fuchsia::hardware::block;
namespace device = ::llcpp::fuchsia::device;
namespace skipblock = ::llcpp::fuchsia::hardware::skipblock;

}  // namespace

zx::status<std::unique_ptr<PartitionClient>> SkipBlockDevicePartitioner::FindPartition(
    const Uuid& type) const {
  zx::status<zx::channel> status = OpenSkipBlockPartition(devfs_root_, type, ZX_SEC(5));
  if (status.is_error()) {
    return status.take_error();
  }

  return zx::ok(new SkipBlockPartitionClient(std::move(status.value())));
}

zx::status<std::unique_ptr<PartitionClient>> SkipBlockDevicePartitioner::FindFvmPartition() const {
  // FVM partition is managed so it should expose a normal block device.
  zx::status<zx::channel> status =
      OpenBlockPartition(devfs_root_, std::nullopt, Uuid(GUID_FVM_VALUE), ZX_SEC(5));
  if (status.is_error()) {
    return status.take_error();
  }

  return zx::ok(new BlockPartitionClient(std::move(status.value())));
}

zx::status<> SkipBlockDevicePartitioner::WipeFvm() const {
  const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
  zx::status<zx::channel> status =
      OpenBlockPartition(devfs_root_, std::nullopt, Uuid(fvm_type), ZX_SEC(3));
  if (status.is_error()) {
    ERROR("Warning: Could not open partition to wipe: %s\n", status.status_string());
    return zx::ok();
  }

  device::Controller::SyncClient block_client(std::move(status.value()));

  auto result = block_client.GetTopologicalPath();
  if (!result.ok()) {
    ERROR("Warning: Could not get name for partition: %s\n", zx_status_get_string(result.status()));
    return zx::error(result.status());
  }
  const auto& response = result.value();
  if (response.result.is_err()) {
    ERROR("Warning: Could not get name for partition: %s\n",
          zx_status_get_string(response.result.err()));
    return zx::error(response.result.err());
  }

  fbl::StringBuffer<PATH_MAX> name_buffer;
  name_buffer.Append(response.result.response().path.data(),
                     static_cast<size_t>(response.result.response().path.size()));

  {
    auto status = zx::make_status(FvmUnbind(devfs_root_, name_buffer.data()));
    if (status.is_error()) {
      // The driver may refuse to bind to a corrupt volume.
      ERROR("Warning: Failed to unbind FVM: %s\n", status.status_string());
    }
  }

  // TODO(fxbug.dev/39761): Clean this up.
  const char* parent = dirname(name_buffer.data());
  constexpr char kDevRoot[] = "/dev/";
  constexpr size_t kDevRootLen = sizeof(kDevRoot) - 1;
  if (strncmp(parent, kDevRoot, kDevRootLen) != 0) {
    ERROR("Warning: Unrecognized partition name: %s\n", parent);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  parent += kDevRootLen;

  zx::channel local, remote;
  {
    auto status = zx::make_status(zx::channel::create(0, &local, &remote));
    if (status.is_error()) {
      ERROR("Warning: Failed to create channel pair: %s\n", status.status_string());
      return status.take_error();
    }
  }
  fdio_cpp::UnownedFdioCaller caller(devfs_root_.get());
  {
    auto status =
        zx::make_status(fdio_service_connect_at(caller.borrow_channel(), parent, remote.release()));
    if (status.is_error()) {
      ERROR("Warning: Unable to open block parent device: %s\n", status.status_string());
      return status.take_error();
    }
  }

  block::Ftl::SyncClient client(std::move(local));
  auto result2 = client.Format();

  return zx::make_status(result2.ok() ? result2.value().status : result2.status());
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

}  // namespace paver
