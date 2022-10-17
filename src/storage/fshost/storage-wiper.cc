// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/storage-wiper.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/watcher.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/hw/gpt.h>

#include <filesystem>
#include <string_view>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/lib/uuid/uuid.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/fshost/block-device.h"
#include "src/storage/fshost/constants.h"

namespace {

constexpr std::string_view kFVMDriverSuffix = "fvm";

// Blobfs options used to format the blob partition for this product.
constexpr blobfs::FilesystemOptions GetProductBlobfsOptions(const fshost_config::Config& config) {
  blobfs::FilesystemOptions options = {};
  // Value should be non-zero only if a product overrides the default value defined within Blobfs.
  if (config.blobfs_initial_inodes() > 0) {
    options.num_inodes = config.blobfs_initial_inodes();
  }
  if (config.blobfs_use_deprecated_padded_format()) {
    options.blob_layout_format = blobfs::BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart;
  }
  return options;
}

// Unbind all children of |device_fd|. Assumes |device_fd| speaks fuchsia.device/Controller.
zx::status<> UnbindChildren(const fbl::unique_fd& device_fd) {
  const fdio_cpp::UnownedFdioCaller device_caller(device_fd);
  auto resp =
      fidl::WireCall(device_caller.borrow_as<fuchsia_device::Controller>())->UnbindChildren();
  if (!resp.ok()) {
    FX_LOGS(ERROR) << "FIDL error when calling UnbindChildren(): "
                   << zx_status_get_string(resp.status());
    return zx::error(resp.status());
  }
  if (resp->is_error() != ZX_OK) {
    FX_LOGS(ERROR) << "Error calling UnbindChildren(): "
                   << zx_status_get_string(resp->error_value());
    return zx::error(resp->error_value());
  }
  return zx::ok();
}

// Bind the FVM driver to |device_fd|. Assumes |device_fd| speaks fuchsia.device/Controller.
zx::status<> BindFvmDriver(const fbl::unique_fd& device_fd) {
  const fdio_cpp::UnownedFdioCaller device_caller(device_fd);
  auto resp = fidl::WireCall(device_caller.borrow_as<fuchsia_device::Controller>())
                  ->Bind(fshost::kFVMDriverPath);
  if (resp.status() != ZX_OK) {
    FX_LOGS(ERROR) << "FIDL error when calling Bind(): " << zx_status_get_string(resp.status());
    return zx::error(resp.status());
  }
  if (resp->is_error() != ZX_OK) {
    FX_LOGS(ERROR) << "Error calling Bind(): " << zx_status_get_string(resp->error_value());
    return zx::error(resp->error_value());
  }
  return zx::ok();
}

// Allocate a new blob and data partition, each with a single slice. On success, returns handle to
// the newly created blob partition. Assumes |fvm_device| speaks
// hardware.block.volume/VolumeManager.
zx::status<fbl::unique_fd> AllocateFvmPartitions(const fbl::unique_fd& fvm_device) {
  // Volumes will be dynamically resized.
  constexpr size_t kInitialSliceCount = 1;

  // Generate FVM layouts and new GUIDs for the blob/data volumes.
  alloc_req_t blob_partition{
      .slice_count = kInitialSliceCount,
      .type = GUID_BLOB_VALUE,
      .name = "blobfs",
  };
  alloc_req_t data_partition{
      .slice_count = kInitialSliceCount,
      .type = GUID_DATA_VALUE,
      .name = "data",
  };
  static_assert(uuid::kUuidSize == sizeof(alloc_req_t::guid));
  const uuid::Uuid blob_guid = uuid::Uuid::Generate();
  std::copy(blob_guid.bytes(), blob_guid.bytes() + uuid::kUuidSize, blob_partition.guid);
  const uuid::Uuid data_guid = uuid::Uuid::Generate();
  std::copy(data_guid.bytes(), data_guid.bytes() + uuid::kUuidSize, data_partition.guid);

  // Allocate new empty blob and data partitions.
  zx::status blob_fd = fs_management::FvmAllocatePartition(fvm_device.get(), &blob_partition);
  if (blob_fd.is_error()) {
    FX_LOGS(ERROR) << "Failed to allocate blob partition: " << blob_fd.status_string();
    return blob_fd;
  }
  if (zx::status status = fs_management::FvmAllocatePartition(fvm_device.get(), &data_partition);
      status.is_error()) {
    FX_LOGS(ERROR) << "Failed to allocate data partition: " << status.status_string();
    return status;
  }

  // Return handle to blob partition so we can format it.
  return blob_fd;
}

zx::status<fbl::unique_fd> WaitForFvm(const std::filesystem::path& device_topo_path) {
  auto watch_func = [](int, int event, const char* filename, void*) -> zx_status_t {
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if (kFVMDriverSuffix.compare(filename) == 0) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };

  const fbl::unique_fd device_fd(open(device_topo_path.c_str(), O_RDWR | O_DIRECTORY));
  if (const zx_status_t status =
          fdio_watch_directory(device_fd.get(), watch_func, ZX_TIME_INFINITE, nullptr);
      status != ZX_ERR_STOP) {
    return zx::error(status);
  }

  const std::filesystem::path fvm_topo_path = device_topo_path / kFVMDriverSuffix;
  fbl::unique_fd fvm_device(open(fvm_topo_path.c_str(), O_RDWR));
  if (!fvm_device.is_valid()) {
    FX_LOGS(ERROR) << "Unable to open FVM by topological path: " << fvm_topo_path;
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok(std::move(fvm_device));
}

}  // namespace

namespace fshost::storage_wiper {

zx::status<fbl::unique_fd> GetFvmBlockDevice(std::string_view ignore_prefix) {
  FX_LOGS(INFO) << "Searching for FVM block device.";
  if (!ignore_prefix.empty()) {
    FX_LOGS(INFO) << "Ignoring devices with prefix: " << ignore_prefix;
  }

  std::optional<fbl::unique_fd> fvm_device;

  for (const auto& block_device : std::filesystem::directory_iterator{kBlockDeviceClassPrefix}) {
    fbl::unique_fd fd(open(block_device.path().c_str(), O_RDWR));
    ZX_DEBUG_ASSERT(fd.is_valid());
    if (!fd.is_valid()) {
      FX_LOGS(ERROR) << "Failed to open device: " << block_device.path().c_str();
      continue;
    }

    const std::string topo_path = GetTopologicalPath(fd.get());
    if (!ignore_prefix.empty()) {
      if (topo_path.find(ignore_prefix) == 0) {
        FX_LOGS(INFO) << "Ignoring device: " << topo_path;
        continue;
      }
    }

    // TODO(fxbug.dev/100049): Try using the partition protocol first to avoid content sniffing. If
    // that fails for some reason, then fall back to content sniffing.
    if (fs_management::DetectDiskFormat(fd.get()) == fs_management::DiskFormat::kDiskFormatFvm) {
      FX_LOGS(INFO) << "Found FVM block device: " << topo_path;
      fvm_device = std::move(fd);
      break;
    }
  }

  if (!fvm_device.has_value()) {
    FX_LOGS(ERROR) << "Unable to find FVM block device.";
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  return zx::ok(*std::move(fvm_device));
}

zx::status<fs_management::StartedSingleVolumeFilesystem> WipeStorage(
    fbl::unique_fd fvm_block_device, const fshost_config::Config& config) {
  const std::filesystem::path device_topo_path = GetTopologicalPath(fvm_block_device.get());
  FX_LOGS(INFO) << "Wiping storage on device: " << device_topo_path;

  FX_LOGS(INFO) << "Unbinding child drivers (FVM/zxcrypt).";
  if (zx::status status = UnbindChildren(fvm_block_device); status.is_error()) {
    FX_LOGS(ERROR) << "Failed to unbind children: " << status.status_string();
    return status.take_error();
  }

  FX_LOGS(INFO) << "Initializing FVM (slice size = " << config.fvm_slice_size() << ").";
  if (const zx_status_t status =
          fs_management::FvmInit(fvm_block_device.get(), config.fvm_slice_size());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize FVM: " << zx_status_get_string(status);
    return zx::error(status);
  }

  FX_LOGS(INFO) << "Binding and waiting for FVM driver.";
  if (zx::status status = BindFvmDriver(fvm_block_device); status.is_error()) {
    FX_LOGS(INFO) << "Failed to bind FVM driver: " << status.status_string();
    return status.take_error();
  }
  zx::status<fbl::unique_fd> fvm_device = WaitForFvm(device_topo_path);
  if (fvm_device.is_error()) {
    FX_LOGS(ERROR) << "Failed to wait for FVM to bind: " << fvm_device.status_string();
    return fvm_device.take_error();
  }

  FX_LOGS(INFO) << "Allocating new partitions.";

  zx::status<fbl::unique_fd> blob_partition = AllocateFvmPartitions(*std::move(fvm_device));
  if (blob_partition.is_error()) {
    FX_LOGS(ERROR) << "Failed to allocate new partitions: " << blob_partition.status_string();
    return blob_partition.take_error();
  }

  FX_LOGS(INFO) << "Formatting Blobfs.";
  {
    const blobfs::FilesystemOptions blobfs_options = GetProductBlobfsOptions(config);
    FX_LOGS(INFO) << "Blobfs filesystem format options: layout = "
                  << blobfs::BlobLayoutFormatToString(blobfs_options.blob_layout_format)
                  << ", num_inodes = " << blobfs_options.num_inodes
                  << ", oldest_min_version = " << blobfs_options.oldest_minor_version;

    zx::status blobfs_device = block_client::RemoteBlockDevice::Create(blob_partition->get());
    if (blobfs_device.is_error()) {
      FX_LOGS(ERROR) << "Failed to create RemoteBlockDevice: " << blobfs_device.status_string();
      return blobfs_device.take_error();
    }
    if (const zx_status_t status =
            blobfs::FormatFilesystem(blobfs_device.value().get(), blobfs_options);
        status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to format Blobfs: " << zx_status_get_string(status);
      return zx::error(status);
    }
  }

  FX_LOGS(INFO) << "Mounting Blobfs.";
  zx::status blobfs = fs_management::Mount(
      *std::move(blob_partition), fs_management::kDiskFormatBlobfs,
      fshost::GetBlobfsMountOptionsForRecovery(config), fs_management::LaunchLogsAsync);
  if (blobfs.is_error()) {
    FX_LOGS(ERROR) << "Failed to mount Blobfs: " << blobfs.status_string();
    return blobfs.take_error();
  }
  return blobfs;
}

}  // namespace fshost::storage_wiper
