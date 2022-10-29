// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/skip-block.h"

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/sys/component/cpp/service_client.h>
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

namespace block = fuchsia_hardware_block;
namespace device = fuchsia_device;
namespace skipblock = fuchsia_hardware_skipblock;

}  // namespace

zx::result<std::unique_ptr<SkipBlockPartitionClient>> SkipBlockDevicePartitioner::FindPartition(
    const Uuid& type) const {
  auto status = OpenSkipBlockPartition(devfs_root_, type, ZX_SEC(5));
  if (status.is_error()) {
    return status.take_error();
  }

  return zx::ok(new SkipBlockPartitionClient(std::move(status.value())));
}

zx::result<std::unique_ptr<PartitionClient>> SkipBlockDevicePartitioner::FindFvmPartition() const {
  // FVM partition is managed so it should expose a normal block device.
  auto status = OpenBlockPartition(devfs_root_, std::nullopt, Uuid(GUID_FVM_VALUE), ZX_SEC(5));
  if (status.is_error()) {
    return status.take_error();
  }

  return zx::ok(new BlockPartitionClient(std::move(status.value())));
}

zx::result<> SkipBlockDevicePartitioner::WipeFvm() const {
  const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
  auto status = OpenBlockPartition(devfs_root_, std::nullopt, Uuid(fvm_type), ZX_SEC(3));
  if (status.is_error()) {
    ERROR("Warning: Could not open partition to wipe: %s\n", status.status_string());
    return zx::ok();
  }

  // Note: converting from |fuchsia.hardware.block.partition/Partition| to
  // |fuchsia.device/Controller| works because devfs connections compose |Controller|.
  fidl::WireSyncClient<device::Controller> block_client(
      fidl::ClientEnd<device::Controller>(status.value().TakeChannel()));

  auto result = block_client->GetTopologicalPath();
  if (!result.ok()) {
    ERROR("Warning: Could not get name for partition: %s\n", zx_status_get_string(result.status()));
    return zx::error(result.status());
  }
  const auto& response = result.value();
  if (response.is_error()) {
    ERROR("Warning: Could not get name for partition: %s\n",
          zx_status_get_string(response.error_value()));
    return zx::error(response.error_value());
  }

  fbl::StringBuffer<PATH_MAX> name_buffer;
  name_buffer.Append(response.value()->path.data(),
                     static_cast<size_t>(response.value()->path.size()));

  {
    auto status = zx::make_result(FvmUnbind(devfs_root_, name_buffer.data()));
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

  fdio_cpp::UnownedFdioCaller caller(devfs_root_);
  zx::result channel = component::ConnectAt<block::Ftl>(caller.directory(), parent);
  if (channel.is_error()) {
    ERROR("Warning: Unable to open block parent device: %s\n", channel.status_string());
    return channel.take_error();
  }

  fidl::WireSyncClient<block::Ftl> client(std::move(channel.value()));
  auto result2 = client->Format();

  return zx::make_result(result2.ok() ? result2.value().status : result2.status());
}

zx::result<> SkipBlockPartitionClient::ReadPartitionInfo() {
  if (!partition_info_) {
    auto result = partition_->GetPartitionInfo();
    auto status = zx::make_result(result.ok() ? result.value().status : result.status());
    if (status.is_error()) {
      ERROR("Failed to get partition info with status: %s\n", status.status_string());
      return status.take_error();
    }
    partition_info_ = result.value().partition_info;
  }
  return zx::ok();
}

zx::result<size_t> SkipBlockPartitionClient::GetBlockSize() {
  auto status = ReadPartitionInfo();
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(static_cast<size_t>(partition_info_->block_size_bytes));
}

zx::result<size_t> SkipBlockPartitionClient::GetPartitionSize() {
  auto status = ReadPartitionInfo();
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(partition_info_->block_size_bytes * partition_info_->partition_block_count);
}

zx::result<> SkipBlockPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  auto status = SkipBlockPartitionClient::GetBlockSize();
  if (status.is_error()) {
    return status.take_error();
  }
  const size_t block_size = status.value();

  zx::vmo dup;
  if (auto status = zx::make_result(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)); status.is_error()) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return status.take_error();
  }

  skipblock::wire::ReadWriteOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = 0,
      .block = 0,
      .block_count = static_cast<uint32_t>(size / block_size),
  };

  auto result = partition_->Read(std::move(operation));
  {
    auto status = zx::make_result(result.ok() ? result.value().status : result.status());
    if (status.is_error()) {
      ERROR("Error reading partition data: %s\n", status.status_string());
      return status.take_error();
    }
  }
  return zx::ok();
}

zx::result<> SkipBlockPartitionClient::Write(const zx::vmo& vmo, size_t size) {
  auto status = SkipBlockPartitionClient::GetBlockSize();
  if (status.is_error()) {
    return status.take_error();
  }
  size_t block_size = status.value();

  zx::vmo dup;
  if (auto status = zx::make_result(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)); status.is_error()) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return status.take_error();
  }

  skipblock::wire::ReadWriteOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = 0,
      .block = 0,
      .block_count = static_cast<uint32_t>(size / block_size),
  };

  auto result = partition_->Write(std::move(operation));
  {
    auto status = zx::make_result(result.ok() ? result.value().status : result.status());
    if (status.is_error()) {
      ERROR("Error writing partition data: %s\n", status.status_string());
      return status.take_error();
    }
  }
  return zx::ok();
}

zx::result<> SkipBlockPartitionClient::WriteBytes(const zx::vmo& vmo, zx_off_t offset,
                                                  size_t size) {
  zx::vmo dup;
  if (auto status = zx::make_result(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)); status.is_error()) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return status.take_error();
  }

  skipblock::wire::WriteBytesOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = 0,
      .offset = offset,
      .size = size,
      .mode = skipblock::wire::WriteBytesMode::kReadModifyEraseWrite,
  };

  auto result = partition_->WriteBytes(std::move(operation));
  auto status = zx::make_result(result.ok() ? result.value().status : result.status());
  if (status.is_error()) {
    ERROR("Error writing partition data: %s\n", status.status_string());
    return status.take_error();
  }
  return zx::ok();
}

zx::result<> SkipBlockPartitionClient::Trim() { return zx::error(ZX_ERR_NOT_SUPPORTED); }

zx::result<> SkipBlockPartitionClient::Flush() { return zx::ok(); }

fidl::ClientEnd<fuchsia_hardware_skipblock::SkipBlock> SkipBlockPartitionClient::GetChannel() {
  return component::MaybeClone(partition_.client_end(), component::AssumeProtocolComposesNode);
}

fbl::unique_fd SkipBlockPartitionClient::block_fd() { return fbl::unique_fd(); }

}  // namespace paver
