// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/bin/start-storage-benchmark/block-device.h"

#include <fcntl.h>
#include <fidl/fuchsia.fxfs/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/spawn.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>
#include <zircon/device/block.h>
#include <zircon/hw/gpt.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <filesystem>
#include <memory>
#include <string_view>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>

#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/lib/storage/fs_management/cpp/mkfs_with_default.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/security/zxcrypt/client.h"
#include "src/storage/bin/start-storage-benchmark/running-filesystem.h"
#include "src/storage/fs_test/crypt_service.h"
#include "src/storage/lib/utils/topological_path.h"

namespace storage_benchmark {
namespace {

using fuchsia_hardware_block_volume::Volume;
using fuchsia_hardware_block_volume::VolumeManager;

zx::result<uint64_t> GetFvmSliceSize(fidl::ClientEnd<VolumeManager> &fvm_client) {
  auto response = fidl::WireCall(fvm_client)->GetInfo();
  if (!response.ok()) {
    fprintf(stderr, "Failed to get fvm slice size: %s\n", response.status_string());
    return zx::error(response.status());
  }
  if (response.value().status != ZX_OK) {
    fprintf(stderr, "Failed to get fvm slice size: %s\n",
            zx_status_get_string(response.value().status));
    return zx::error(response.value().status);
  }
  return zx::ok(response.value().info->slice_size);
}

// Returns the number of slices required to create a volume of |volume_size| in fvm.
zx::result<uint64_t> GetSliceCount(fidl::ClientEnd<VolumeManager> &fvm_client,
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
  BlockDeviceFilesystem(std::unique_ptr<fs_management::SingleVolumeFilesystemInterface> filesystem,
                        FvmVolume volume)
      : volume_(std::move(volume)), filesystem_(std::move(filesystem)) {}

  zx::result<fidl::ClientEnd<fuchsia_io::Directory>> GetFilesystemRoot() const override {
    return filesystem_->DataRoot();
  }

 private:
  FvmVolume volume_;
  std::unique_ptr<fs_management::SingleVolumeFilesystemInterface> filesystem_;
};

// Whilst this runs as a v1 component, we use this slightly hacky way of launching the crypt
// service.  Once we have migrated to a v2 component, then we should be able to instantiate the
// crypt service via the manifest and run it as a child.
zx::result<zx::channel> GetCryptService() {
  static zx_handle_t svc_directory = [] {
    zx::process process;
    char error_message[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    constexpr char kFxfsCryptPath[] = "/pkg/bin/fxfs_crypt";
    const char *argv[] = {kFxfsCryptPath, nullptr};

    auto outgoing_directory_or = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (outgoing_directory_or.is_error()) {
      fprintf(stderr, "Unable to create endpoints: %s\n", outgoing_directory_or.status_string());
      return ZX_HANDLE_INVALID;
    }
    fdio_spawn_action_t actions[] = {
        {.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
         .h = {.id = PA_DIRECTORY_REQUEST,
               .handle = outgoing_directory_or->server.TakeChannel().release()}}};
    if (fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kFxfsCryptPath, argv, nullptr, 1,
                       actions, process.reset_and_get_address(), error_message)) {
      fprintf(stderr, "Failed to launch crypt service: %s\n", error_message);
      return ZX_HANDLE_INVALID;
    }

    auto service_endpoints_or = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (service_endpoints_or.is_error()) {
      fprintf(stderr, "Unable to create endpoints: %s\n", service_endpoints_or.status_string());
      return ZX_HANDLE_INVALID;
    }

    if (auto result = fidl::WireCall(outgoing_directory_or->client)
                          ->Open(fuchsia_io::wire::OpenFlags::kRightReadable |
                                     fuchsia_io::wire::OpenFlags::kRightWritable,
                                 0, "svc",
                                 fidl::ServerEnd<fuchsia_io::Node>(
                                     service_endpoints_or->server.TakeChannel()));
        result.status() != ZX_OK) {
      fprintf(stderr, "Failed to open svc directory: %s\n", result.status_string());
      return ZX_HANDLE_INVALID;
    }

    if (auto status = fs_test::SetUpCryptWithRandomKeys(service_endpoints_or->client);
        status.is_error()) {
      fprintf(stderr, "Unable to set up the crypt service: %s\n", status.status_string());
      return ZX_HANDLE_INVALID;
    }

    return service_endpoints_or->client.TakeChannel().release();
  }();

  if (svc_directory == ZX_HANDLE_INVALID)
    return zx::error(ZX_ERR_INTERNAL);

  if (auto crypt_service_or = component::ConnectAt<fuchsia_fxfs::Crypt>(
          fidl::UnownedClientEnd<fuchsia_io::Directory>(svc_directory));
      crypt_service_or.is_error()) {
    fprintf(stderr, "Unable to connect to crypt service: %s\n", crypt_service_or.status_string());
    return crypt_service_or.take_error();
  } else {
    return zx::ok(crypt_service_or->TakeChannel());
  }
}

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
  auto volume_client = component::Connect<Volume>(path_.c_str());
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
  if (response.value().status != ZX_OK) {
    fprintf(stderr, "Failed to destroy volume: %s\n",
            zx_status_get_string(response.value().status));
  }
}

zx::result<FvmVolume> FvmVolume::Create(fidl::ClientEnd<VolumeManager> &fvm_client,
                                        uint64_t partition_size) {
  zx::result<uint64_t> slice_count = GetSliceCount(fvm_client, partition_size);
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
  if (response.value().status != ZX_OK) {
    fprintf(stderr, "Failed to create volume: %s\n", zx_status_get_string(response.value().status));
    return zx::error(response.value().status);
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

zx::result<std::string> FindFvmBlockDevicePath() {
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
  FX_LOGS(ERROR) << "Failed to find fvm's block device";
  return zx::error(ZX_ERR_NOT_FOUND);
}

zx::result<fidl::ClientEnd<VolumeManager>> ConnectToFvm(const std::string &fvm_block_device_path) {
  auto fvm_block_topological_path = storage::GetTopologicalPath(fvm_block_device_path);
  if (fvm_block_topological_path.is_error()) {
    fprintf(stderr, "Failed to get the topological path to fvm's block device: %s\n",
            fvm_block_topological_path.status_string());
    return fvm_block_topological_path.take_error();
  }

  std::string fvm_path = *fvm_block_topological_path + "/fvm";
  return component::Connect<VolumeManager>(fvm_path.c_str());
}

zx::result<> FormatBlockDevice(const std::string &block_device_path,
                               fs_management::DiskFormat format) {
  fs_management::MkfsOptions mkfs_options;

  if (format == fs_management::kDiskFormatFxfs) {
    mkfs_options.component_url = "#meta/fxfs";
    mkfs_options.component_child_name = "fxfs";
  }

  zx::result<> status;
  if (format == fs_management::kDiskFormatFxfs) {
    auto service = GetCryptService();
    if (service.is_error()) {
      fprintf(stderr, "Failed to get crypt service: %s\n", service.status_string());
      return service.take_error();
    }
    status = fs_management::MkfsWithDefault(block_device_path.c_str(), format,
                                            fs_management::LaunchStdioSync, mkfs_options,
                                            *std::move(service));
  } else {
    status = zx::make_result(fs_management::Mkfs(block_device_path.c_str(), format,
                                                 fs_management::LaunchStdioSync, mkfs_options));
  }
  if (status.is_error()) {
    // Convert the std::string_view to std::string to guarantee that it's null terminated.
    std::string format_name(DiskFormatString(format));
    fprintf(stderr, "Failed to format %s with %s\n", block_device_path.c_str(),
            format_name.c_str());
  }
  return status;
}

zx::result<std::unique_ptr<RunningFilesystem>> StartBlockDeviceFilesystem(
    const std::string &block_device_path, fs_management::DiskFormat format, FvmVolume fvm_volume) {
  fs_management::MountOptions mount_options;
  fbl::unique_fd volume_fd(open(block_device_path.c_str(), O_RDWR));
  std::unique_ptr<fs_management::SingleVolumeFilesystemInterface> fs;
  if (format == fs_management::kDiskFormatFxfs) {
    mount_options.crypt_client = [] {
      if (auto service_or = GetCryptService(); service_or.is_error()) {
        fprintf(stderr, "Failed to get crypt service: %s\n", service_or.status_string());
        return zx::channel();
      } else {
        return *std::move(service_or);
      }
    };
    mount_options.component_url = "#meta/fxfs";
    mount_options.component_child_name = "fxfs";
    auto mounted_filesystem = fs_management::MountMultiVolumeWithDefault(
        std::move(volume_fd), format, mount_options, fs_management::LaunchStdioAsync);
    if (mounted_filesystem.is_error()) {
      std::string format_name(DiskFormatString(format));
      fprintf(stderr, "Failed to mount %s as %s\n", block_device_path.c_str(), format_name.c_str());
      return mounted_filesystem.take_error();
    }
    fs = std::make_unique<fs_management::StartedSingleVolumeMultiVolumeFilesystem>(
        std::move(*mounted_filesystem));
  } else {
    auto mounted_filesystem =
        Mount(std::move(volume_fd), format, mount_options, fs_management::LaunchStdioAsync);
    if (mounted_filesystem.is_error()) {
      std::string format_name(DiskFormatString(format));
      fprintf(stderr, "Failed to mount %s as %s\n", block_device_path.c_str(), format_name.c_str());
      return mounted_filesystem.take_error();
    }
    fs = std::make_unique<fs_management::StartedSingleVolumeFilesystem>(
        std::move(*mounted_filesystem));
  }
  return zx::ok(std::make_unique<BlockDeviceFilesystem>(std::move(fs), std::move(fvm_volume)));
}

zx::result<std::string> CreateZxcryptVolume(const std::string &device_path) {
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
  auto status = zx::make_result(volume_manager.OpenClient(zx::sec(2), driver_chan));
  if (status.is_error()) {
    fprintf(stderr, "Failed to bind zxcrypt driver on %s\n", device_path.c_str());
    return status.take_error();
  }

  zxcrypt::EncryptedVolumeClient volume(std::move(driver_chan));
  status = zx::make_result(volume.FormatWithImplicitKey(0));
  if (status.is_error()) {
    fprintf(stderr, "Failed to create zxcrypt volume on %s\n", device_path.c_str());
    return status.take_error();
  }

  status = zx::make_result(volume.UnsealWithImplicitKey(0));
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
