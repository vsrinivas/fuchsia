// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/bin/start-storage-benchmark/block-device.h"

#include <fcntl.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <zircon/device/block.h>
#include <zircon/hw/gpt.h>
#include <zircon/status.h>

#include <filesystem>
#include <memory>
#include <string_view>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>

#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/security/zxcrypt/client.h"
#include "src/storage/bin/start-storage-benchmark/running-filesystem.h"
#include "src/storage/lib/utils/topological_path.h"

namespace storage_benchmark {
namespace {

using fuchsia_hardware_block_volume::Volume;
using fuchsia_hardware_block_volume::VolumeManager;

zx::status<uint64_t> GetFvmSliceSize(fidl::ClientEnd<VolumeManager> &fvm_client) {
  auto response = fidl::WireCall(fvm_client)->GetInfo();
  if (!response.ok()) {
    fprintf(stderr, "Failed to get fvm slice size: %s\n", response.status_string());
    return zx::error(response.status());
  }
  if (response->status != ZX_OK) {
    fprintf(stderr, "Failed to get fvm slice size: %s\n", zx_status_get_string(response->status));
    return zx::error(response->status);
  }
  return zx::ok(response->info->slice_size);
}

// Returns the number of slices required to create a volume of |volume_size| in fvm.
zx::status<uint64_t> GetSliceCount(fidl::ClientEnd<VolumeManager> &fvm_client,
                                   uint64_t volume_size) {
  if (volume_size == 0) {
    // If no volume size was specified then only use 1 slice and let the filesystem grow within
    // fvm as needed. This only works fvm-aware filesystems like blobfs and minfs.
    return zx::ok(1);
  }
  auto slice_size = GetFvmSliceSize(fvm_client);
  if (slice_size.is_error()) {
    return slice_size.take_error();
  }
  return zx::ok(fbl::round_up(volume_size, *slice_size) / *slice_size);
}

// Wrapper around a |MountedFilesystem| to meet the |RunningFilesystem| interface.
class BlockDeviceFilesystem : public RunningFilesystem {
 public:
  // Takes ownership of the volume to ensure that the volume outlives the mounted filesystem.
  explicit BlockDeviceFilesystem(fs_management::MountedFilesystem filesystem, FvmVolume volume)
      : volume_(std::move(volume)), filesystem_(std::move(filesystem)) {}

  zx::status<fidl::ClientEnd<fuchsia_io::Directory>> GetFilesystemRoot() const override {
    return fs_management::FsRootHandle(filesystem_.export_root());
  }

 private:
  FvmVolume volume_;
  fs_management::MountedFilesystem filesystem_;
};

}  // namespace

FvmVolume::FvmVolume(FvmVolume &&other) noexcept : path_(std::move(other.path_)) {
  other.path_.clear();
}

FvmVolume &FvmVolume::operator=(FvmVolume &&other) noexcept {
  path_ = std::move(other.path_);
  other.path_.clear();
  return *this;
}

FvmVolume::~FvmVolume() {
  if (path_.empty()) {
    return;
  }
  auto volume_client = service::Connect<Volume>(path_.c_str());
  if (volume_client.is_error()) {
    fprintf(stderr, "Failed to connect to volume: %s %s\n", path_.c_str(),
            volume_client.status_string());
    return;
  }
  auto response = fidl::WireCall(*volume_client)->Destroy();
  if (!response.ok()) {
    fprintf(stderr, "Failed to destroy volume: %s\n", response.status_string());
    return;
  }
  if (response->status != ZX_OK) {
    fprintf(stderr, "Failed to destroy volume: %s\n", zx_status_get_string(response->status));
  }
}

zx::status<FvmVolume> FvmVolume::Create(fidl::ClientEnd<VolumeManager> &fvm_client,
                                        uint64_t partition_size) {
  zx::status<uint64_t> slice_count = GetSliceCount(fvm_client, partition_size);
  if (slice_count.is_error()) {
    return slice_count.take_error();
  }

  uuid::Uuid unique_instance = uuid::Uuid::Generate();
  fuchsia_hardware_block_partition::wire::Guid instance_guid;
  memcpy(instance_guid.value.data(), unique_instance.bytes(), BLOCK_GUID_LEN);
  fuchsia_hardware_block_partition::wire::Guid type_guid = GUID_TEST_VALUE;
  auto response =
      fidl::WireCall(fvm_client)
          ->AllocatePartition(*slice_count, type_guid, instance_guid, fidl::StringView("benchmark"),
                              fuchsia_hardware_block_volume::wire::kAllocatePartitionFlagInactive);
  if (!response.ok()) {
    fprintf(stderr, "Failed to create volume: %s\n", response.status_string());
    return zx::error(response.status());
  }
  if (response->status != ZX_OK) {
    fprintf(stderr, "Failed to create volume: %s\n", zx_status_get_string(response->status));
    return zx::error(response->status);
  }

  fs_management::PartitionMatcher matcher{
      .type_guid = type_guid.value.data(),
      .instance_guid = instance_guid.value.data(),
  };
  std::string path;
  auto fd = fs_management::OpenPartition(&matcher, ZX_SEC(10), &path);
  if (fd.is_error()) {
    fprintf(stderr, "Failed to find newly created volume\n");
    return zx::error(ZX_ERR_TIMED_OUT);
  }
  return zx::ok(FvmVolume(std::move(path)));
}

zx::status<std::string> FindFvmBlockDevicePath() {
  for (const auto &block_device : std::filesystem::directory_iterator("/dev/class/block")) {
    fbl::unique_fd fd(open(block_device.path().c_str(), O_RDWR));
    if (!fd.is_valid()) {
      continue;
    }
    auto disk_format = fs_management::DetectDiskFormat(fd.get());
    if (disk_format == fs_management::DiskFormat::kDiskFormatFvm) {
      return zx::ok(block_device.path());
    }
  }
  fprintf(stderr, "Failed to find fvm's block device\n");
  return zx::error(ZX_ERR_NOT_FOUND);
}

zx::status<fidl::ClientEnd<VolumeManager>> ConnectToFvm(const std::string &fvm_block_device_path) {
  auto fvm_block_topological_path = storage::GetTopologicalPath(fvm_block_device_path);
  if (fvm_block_topological_path.is_error()) {
    fprintf(stderr, "Failed to get the topological path to fvm's block device: %s\n",
            fvm_block_topological_path.status_string());
    return fvm_block_topological_path.take_error();
  }

  std::string fvm_path = *fvm_block_topological_path + "/fvm";
  return service::Connect<VolumeManager>(fvm_path.c_str());
}

zx::status<> FormatBlockDevice(const std::string &block_device_path,
                               fs_management::DiskFormat format) {
  fs_management::MkfsOptions mkfs_options;
  zx::status<> status = zx::make_status(
      fs_management::Mkfs(block_device_path.c_str(), format, launch_stdio_sync, mkfs_options));
  if (status.is_error()) {
    // Convert the std::string_view to std::string to guarantee that it's null terminated.
    std::string format_name(DiskFormatString(format));
    fprintf(stderr, "Failed to format %s with %s\n", block_device_path.c_str(),
            format_name.c_str());
  }
  return status;
}

zx::status<std::unique_ptr<RunningFilesystem>> StartBlockDeviceFilesystem(
    const std::string &block_device_path, fs_management::DiskFormat format, FvmVolume fvm_volume) {
  fs_management::MountOptions mount_options;
  fbl::unique_fd volume_fd(open(block_device_path.c_str(), O_RDWR));
  auto mounted_filesystem = fs_management::Mount(std::move(volume_fd), nullptr, format,
                                                 mount_options, launch_stdio_async);
  if (mounted_filesystem.is_error()) {
    std::string format_name(DiskFormatString(format));
    fprintf(stderr, "Failed to mount %s as %s\n", block_device_path.c_str(), format_name.c_str());
    return mounted_filesystem.take_error();
  }
  return zx::ok(std::make_unique<BlockDeviceFilesystem>(std::move(mounted_filesystem).value(),
                                                        std::move(fvm_volume)));
}

zx::status<std::string> CreateZxcryptVolume(const std::string &device_path) {
  fbl::unique_fd fd(open(device_path.c_str(), O_RDWR));
  if (!fd) {
    fprintf(stderr, "Failed to open %s\n", device_path.c_str());
    return zx::error(ZX_ERR_BAD_STATE);
  }
  fbl::unique_fd dev_fd(open("/dev", O_RDONLY));
  if (!dev_fd) {
    fprintf(stderr, "Failed to open /dev\n");
    return zx::error(ZX_ERR_BAD_STATE);
  }

  zxcrypt::VolumeManager volume_manager(std::move(fd), std::move(dev_fd));
  zx::channel driver_chan;
  auto status = zx::make_status(volume_manager.OpenClient(zx::sec(2), driver_chan));
  if (status.is_error()) {
    fprintf(stderr, "Failed to bind zxcrypt driver on %s\n", device_path.c_str());
    return status.take_error();
  }

  zxcrypt::EncryptedVolumeClient volume(std::move(driver_chan));
  status = zx::make_status(volume.FormatWithImplicitKey(0));
  if (status.is_error()) {
    fprintf(stderr, "Failed to create zxcrypt volume on %s\n", device_path.c_str());
    return status.take_error();
  }

  status = zx::make_status(volume.UnsealWithImplicitKey(0));
  if (status.is_error()) {
    fprintf(stderr, "Failed to unseal zxcrypt volume: %s\n", status.status_string());
    return status.take_error();
  }

  auto topological_path = storage::GetTopologicalPath(device_path);
  if (topological_path.is_error()) {
    fprintf(stderr, "Failed to get topological path for %s: %s\n", device_path.c_str(),
            topological_path.status_string());
    return topological_path.take_error();
  }
  return zx::ok(topological_path.value() + "/zxcrypt/unsealed/block");
}

}  // namespace storage_benchmark
