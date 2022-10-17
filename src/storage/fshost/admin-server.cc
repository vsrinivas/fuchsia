// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/admin-server.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/markers.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/markers.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fd.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/hw/gpt.h>
#include <zircon/time.h>

#include <safemath/safe_math.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/lib/storage/fs_management/cpp/launch.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/lib/storage/fs_management/cpp/options.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/storage/fshost/block-device.h"
#include "src/storage/fshost/constants.h"
#include "src/storage/fshost/filesystem-mounter.h"
#include "src/storage/fshost/fxfs.h"
#include "src/storage/fshost/storage-wiper.h"
#include "src/storage/fshost/utils.h"

namespace fshost {

namespace {

constexpr zx_duration_t kOpenPartitionDuration = ZX_SEC(10);

}  // namespace

fbl::RefPtr<fs::Service> AdminServer::Create(FsManager* fs_manager,
                                             const fshost_config::Config& config,
                                             async_dispatcher* dispatcher,
                                             BlockWatcher& block_watcher) {
  return fbl::MakeRefCounted<fs::Service>([dispatcher, fs_manager, config, &block_watcher](
                                              fidl::ServerEnd<fuchsia_fshost::Admin> chan) {
    zx_status_t status = fidl::BindSingleInFlightOnly(
        dispatcher, std::move(chan),
        std::make_unique<AdminServer>(fs_manager, config, block_watcher));
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to bind admin service: " << zx_status_get_string(status);
      return status;
    }
    return ZX_OK;
  });
}

void AdminServer::Mount(MountRequestView request, MountCompleter::Sync& completer) {
  std::string device_path;
  if (auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_device::Controller>(
                                       request->device.channel().borrow()))
                        ->GetTopologicalPath();
      result.status() != ZX_OK) {
    FX_LOGS(WARNING) << "Unable to get device topological path (FIDL error): "
                     << zx_status_get_string(result.status());
  } else if (result->is_error()) {
    FX_LOGS(WARNING) << "Unable to get device topological path: "
                     << zx_status_get_string(result->error_value());
  } else {
    device_path = result->value()->path.get();
  }

  fbl::unique_fd fd;
  if (zx_status_t status =
          fdio_fd_create(request->device.TakeChannel().release(), fd.reset_and_get_address());
      status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  fs_management::DiskFormat df = fs_management::DetectDiskFormat(fd.get());

  const std::string name(request->name.get());
  const auto& o = request->options;
  std::string compression_algorithm;
  if (o.has_write_compression_algorithm())
    compression_algorithm = o.write_compression_algorithm().get();
  fs_management::MountOptions options = {
      .readonly = o.has_read_only() && o.read_only(),
      .verbose_mount = o.has_verbose() && o.verbose(),
  };
  if (o.has_write_compression_algorithm()) {
    options.write_compression_algorithm = compression_algorithm;
  }

  FX_LOGS(INFO) << "Mounting " << fs_management::DiskFormatString(df) << " filesystem at /mnt/"
                << name;

  async_dispatcher_t* dispatcher = async_get_default_dispatcher();

  // Launching a filesystem requirees accessing the loader which is running on the same async loop
  // that we're running on but it's only running on one thread, so if we're not careful, we'll end
  // up with a deadlock.  To avoid this, spawn a separate thread.  Unfortunately, this isn't
  // thread-safe if we're shutting down, but since mounting is a debug only thing for now, we don't
  // worry about it.
  std::thread thread([device_path = std::move(device_path), name, completer = completer.ToAsync(),
                      options = std::move(options), fd = std::move(fd), df,
                      fs_manager = fs_manager_, dispatcher]() mutable {
    static int mount_index = 0;
    std::string component_child_name = name + "." + std::to_string(mount_index++);
    options.component_child_name = component_child_name.c_str();
    options.component_collection_name = "fs-collection";

    // TODO(fxbug.dev/93066): Support mounting multi-volume filesystems as well.
    auto mounted_filesystem =
        fs_management::Mount(std::move(fd), df, options, fs_management::LaunchLogsAsync);
    if (mounted_filesystem.is_error()) {
      FX_LOGS(WARNING) << "Mount failed: " << mounted_filesystem.status_string();
      completer.ReplyError(mounted_filesystem.error_value());
      return;
    }

    // fs_manager isn't thread-safe, so we have to post back on to the async loop to attach the
    // mount.
    async::PostTask(
        dispatcher,
        [device_path = std::move(device_path), mounted_filesystem = std::move(*mounted_filesystem),
         name = std::move(name), fs_manager, completer = std::move(completer)]() mutable {
          if (zx_status_t status =
                  fs_manager->AttachMount(device_path, std::move(mounted_filesystem), name);
              status != ZX_OK) {
            FX_LOGS(WARNING) << "Failed to attach mount: " << zx_status_get_string(status);
            completer.ReplyError(status);
            return;
          }
          completer.ReplySuccess();
        });
  });

  thread.detach();
}

void AdminServer::Unmount(UnmountRequestView request, UnmountCompleter::Sync& completer) {
  FX_LOGS(INFO) << "Unmounting " << request->name.get();
  if (zx_status_t status = fs_manager_->DetachMount(request->name.get()); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to unmount: " << zx_status_get_string(status);
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void AdminServer::GetDevicePath(GetDevicePathRequestView request,
                                GetDevicePathCompleter::Sync& completer) {
  if (auto device_path_or = fs_manager_->GetDevicePath(request->fs_id); device_path_or.is_error()) {
    completer.ReplyError(device_path_or.status_value());
  } else {
    completer.ReplySuccess(fidl::StringView::FromExternal(*device_path_or));
  }
}

void AdminServer::WriteDataFile(WriteDataFileRequestView request,
                                WriteDataFileCompleter::Sync& completer) {
  auto status = WriteDataFileInner(request);
  if (status.is_ok()) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status.status_value());
  }
}

zx::result<> AdminServer::WriteDataFileInner(WriteDataFileRequestView request) {
  // Recovery builds set `fvm_ramdisk`, Zedboot builds set `netboot`.  Either way, the data volume
  // won't be automatically mounted in this configuration, which is all we need to ensure.
  if (!config_.fvm_ramdisk() && !config_.netboot()) {
    FX_LOGS(INFO) << "Can't WriteDataFile from a non-recovery build; fvm_ramdisk must be set.";
    return zx::error(ZX_ERR_BAD_STATE);
  }
  if (!files::IsValidCanonicalPath(request->filename.get())) {
    FX_LOGS(WARNING) << "Bad path " << request->filename.get();
    return zx::error(ZX_ERR_BAD_PATH);
  }

  uint64_t content_size = 0;
  if (zx_status_t status = request->payload.get_prop_content_size(&content_size); status != ZX_OK) {
    if (status = request->payload.get_size(&content_size); status != ZX_OK) {
      return zx::error(status);
    }
  }
  ssize_t content_ssize;
  if (!safemath::MakeCheckedNum<size_t>(content_size)
           .Cast<ssize_t>()
           .AssignIfValid(&content_ssize)) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  ZX_DEBUG_ASSERT(!config_.ramdisk_prefix().empty());
  const fs_management::PartitionMatcher fvm_matcher{
      .detected_disk_format = fs_management::kDiskFormatFvm,
      .ignore_prefix = config_.ramdisk_prefix(),
  };
  auto fvm = OpenPartition(&fvm_matcher, kOpenPartitionDuration, nullptr);
  if (fvm.is_error()) {
    FX_PLOGS(ERROR, fvm.status_value()) << "Failed to find FVM";
    return zx::error(fvm.status_value());
  }

  fs_management::DiskFormat format = fs_management::kDiskFormatMinfs;
  if (config_.data_filesystem_format() == "fxfs") {
    format = fs_management::kDiskFormatFxfs;
  } else if (config_.data_filesystem_format() == "f2fs") {
    format = fs_management::kDiskFormatF2fs;
  }

  const std::string data_partition_names[] = {std::string(kDataPartitionLabel),
                                              std::string(kLegacyDataPartitionLabel)};
  const char* c_data_partition_names[] = {data_partition_names[0].c_str(),
                                          data_partition_names[1].c_str()};
  constexpr uint8_t kDataGuid[] = GUID_DATA_VALUE;
  std::string fvm_path = GetTopologicalPath(fvm->get());
  const fs_management::PartitionMatcher data_matcher{
      .type_guid = kDataGuid,
      .labels = c_data_partition_names,
      .num_labels = 2,
      .parent_device = fvm_path,
      .ignore_if_path_contains = "zxcrypt/unsealed",
  };
  auto partition = OpenPartition(&data_matcher, kOpenPartitionDuration, nullptr);
  if (partition.is_error()) {
    FX_PLOGS(ERROR, partition.status_value()) << "Failed to find data partition";
    return partition.take_error();
  }
  FX_LOGS(INFO) << "Using data path " << GetTopologicalPath(partition->get());

  auto detected_format = fs_management::DetectDiskFormat(partition->get());
  bool inside_zxcrypt = false;
  if (format != fs_management::kDiskFormatFxfs && !config_.no_zxcrypt()) {
    // For non-Fxfs configurations, we expect zxcrypt to be present and have already been formatted
    // (if needed) by the block watcher.
    std::string zxcrypt_path = GetTopologicalPath(partition->get()) + "/zxcrypt/unsealed";
    const fs_management::PartitionMatcher zxcrypt_matcher{
        .type_guid = kDataGuid,
        .labels = c_data_partition_names,
        .num_labels = 2,
        .parent_device = zxcrypt_path,
    };
    partition = OpenPartition(&zxcrypt_matcher, kOpenPartitionDuration, nullptr);
    if (partition.is_error()) {
      FX_PLOGS(ERROR, partition.status_value()) << "Failed to find inner data partition";
      return partition.take_error();
    }
    inside_zxcrypt = true;
  }
  std::string partition_path = GetTopologicalPath(partition->get());
  FX_LOGS(INFO) << "Using data partition at " << partition_path << ", has format "
                << fs_management::DiskFormatString(detected_format);

  std::optional<StartedFilesystem> started_fs;
  fidl::ClientEnd<fuchsia_io::Directory> data_root;
  if (detected_format != format) {
    FX_LOGS(INFO) << "Data partition is not in expected format; reformatting";
    if (format != fs_management::kDiskFormatMinfs) {
      // Minfs is FVM-aware and will grow as needed, but other filesystems require a pre-allocation.
      zx::channel block_device;
      if (zx_status_t status =
              fdio_fd_clone(partition->get(), block_device.reset_and_get_address());
          status != ZX_OK) {
        return zx::error(status);
      }
      fidl::ClientEnd<fuchsia_hardware_block_volume::Volume> volume_client(std::move(block_device));
      uint64_t target_size = config_.data_max_bytes();
      if (format == fs_management::kDiskFormatF2fs) {
        auto query_result = fidl::WireCall(volume_client)->GetVolumeInfo();
        if (query_result.status() != ZX_OK) {
          return zx::error(query_result.status());
        }
        if (query_result.value().status != ZX_OK) {
          return zx::error(query_result.value().status);
        }
        const uint64_t slice_size = query_result.value().manager->slice_size;
        uint64_t required_size = fbl::round_up(kDefaultF2fsMinBytes, slice_size);
        // f2fs always requires at least a certain size.
        if (inside_zxcrypt) {
          // Allocate an additional slice for zxcrypt metadata.
          required_size += slice_size;
        }
        target_size = std::max(target_size, required_size);
      }
      FX_LOGS(INFO) << "Resizing data volume, target = " << target_size << " bytes";
      auto actual_size = ResizeVolume(volume_client, target_size, inside_zxcrypt);
      if (actual_size.is_error()) {
        FX_PLOGS(ERROR, actual_size.status_value()) << "Failed to resize volume";
        return actual_size.take_error();
      }
      if (format == fs_management::kDiskFormatF2fs && *actual_size < kDefaultF2fsMinBytes) {
        FX_LOGS(ERROR) << "Only allocated " << *actual_size << " bytes but needed "
                       << kDefaultF2fsMinBytes;
        return zx::error(ZX_ERR_NO_SPACE);
      } else if (*actual_size < target_size) {
        FX_LOGS(WARNING) << "Only allocated " << *actual_size << " bytes";
      }
    }
    if (format == fs_management::kDiskFormatFxfs) {
      zx::channel block_device;
      if (zx_status_t status =
              fdio_fd_clone(partition->get(), block_device.reset_and_get_address());
          status != ZX_OK) {
        return zx::error(status);
      }
      auto fxfs = FormatFxfsAndInitDataVolume(
          fidl::ClientEnd<fuchsia_hardware_block::Block>(std::move(block_device)), config_);
      if (fxfs.is_error()) {
        FX_PLOGS(ERROR, fxfs.status_value()) << "Failed to format data partition";
        return fxfs.take_error();
      }
      if (auto status = fxfs->second->DataRoot(); status.is_ok()) {
        data_root = std::move(*status);
      } else {
        FX_PLOGS(ERROR, status.status_value()) << "Failed to get data root";
        return status.take_error();
      }
      started_fs.emplace(std::move(fxfs->first));
    } else {
      fs_management::MkfsOptions options;
      if (!fs_management::DiskFormatComponentUrl(format).empty()) {
        options.component_child_name = fs_management::DiskFormatString(format);
      }
      if (zx_status_t status = fs_management::Mkfs(partition_path.c_str(), format,
                                                   fs_management::LaunchStdioAsync, options);
          status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "Failed to format data partition";
        return zx::error(status);
      }
    }
  }
  if (!data_root) {
    fs_management::MountOptions options;
    if (format == fs_management::kDiskFormatFxfs) {
      options.component_child_name = fs_management::DiskFormatString(format);
      auto fxfs = fs_management::MountMultiVolume(std::move(*partition), format, options,
                                                  fs_management::LaunchStdioAsync);
      if (fxfs.is_error()) {
        FX_PLOGS(ERROR, fxfs.status_value()) << "Failed to open data partition";
        return fxfs.take_error();
      }
      auto data_volume = UnwrapDataVolume(*fxfs, config_);
      if (data_volume.is_error()) {
        FX_PLOGS(ERROR, data_volume.status_value()) << "Failed to unwrap data volume";
        return data_volume.take_error();
      }
      if (auto status = (*data_volume)->DataRoot(); status.is_ok()) {
        data_root = std::move(*status);
      } else {
        FX_PLOGS(ERROR, status.status_value()) << "Failed to get data root";
        return status.take_error();
      }
      started_fs.emplace(std::move(*fxfs));
    } else {
      if (!fs_management::DiskFormatComponentUrl(format).empty()) {
        options.component_child_name = fs_management::DiskFormatString(format);
      }
      auto fs = fs_management::Mount(std::move(*partition), format, options,
                                     fs_management::LaunchStdioAsync);
      if (fs.is_error()) {
        FX_PLOGS(ERROR, fs.status_value()) << "Failed to open data partition";
        return fs.take_error();
      }
      if (auto status = fs->DataRoot(); status.is_ok()) {
        data_root = std::move(*status);
      } else {
        FX_PLOGS(ERROR, status.status_value()) << "Failed to get data root";
        return status.take_error();
      }
      started_fs.emplace(std::move(*fs));
    }
  }
  fbl::unique_fd root;
  if (zx_status_t status = fdio_fd_create(data_root.handle()->get(), root.reset_and_get_address());
      status != ZX_OK) {
    return zx::error(status);
  }
  fzl::OwnedVmoMapper mapper;
  if (zx_status_t status = mapper.Map(std::move(request->payload), content_size); status != ZX_OK) {
    return zx::error(status);
  }
  const std::string path(request->filename.get());
  const std::string base = files::GetDirectoryName(path);
  if (!base.empty() && !files::CreateDirectoryAt(root.get(), base)) {
    FX_LOGS(ERROR) << "Failed to create parent directory " << base;
    return zx::error(ZX_ERR_IO);
  }
  if (!files::WriteFileAt(root.get(), path, static_cast<const char*>(mapper.start()),
                          content_ssize)) {
    FX_LOGS(ERROR) << "Failed to write file " << request->filename.data();
    return zx::error(ZX_ERR_IO);
  }

  return zx::ok();
}

void AdminServer::WipeStorage(WipeStorageRequestView request,
                              WipeStorageCompleter::Sync& completer) {
  if (!config_.fvm_ramdisk()) {
    // WipeStorage should only be invoked during recovery (when `fvm_ramdisk` will be set).
    FX_LOGS(ERROR) << "WipeStorage can only be invoked from a recovery context.";
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  if (!request->blobfs_root.is_valid()) {
    FX_LOGS(ERROR) << "Invalid directory handle passed to WipeStorage.";
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  // We need to pause the block watcher to make sure fshost doesn't try to mount or format any of
  // the newly provisioned volumes in the FVM.
  if (!block_watcher_.IsPaused()) {
    FX_LOGS(INFO) << "Pausing block watcher.";
    if (const zx_status_t status = block_watcher_.Pause(); status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to pause block watcher: " << zx_status_get_string(status);
      completer.ReplyError(status);
      return;
    }
  } else {
    FX_LOGS(INFO) << "Block watcher already paused.";
  }

  // Find the first non-ramdisk FVM partition to wipe.
  ZX_DEBUG_ASSERT(!config_.ramdisk_prefix().empty());
  zx::status<fbl::unique_fd> fvm_device =
      storage_wiper::GetFvmBlockDevice(config_.ramdisk_prefix());
  if (fvm_device.is_error()) {
    FX_LOGS(ERROR) << "Failed get FVM block device: " << fvm_device.status_string();
    completer.ReplyError(fvm_device.status_value());
    return;
  }

  // Wipe and reprovision the FVM partition with the product/board configured values.
  zx::status<fs_management::StartedSingleVolumeFilesystem> blobfs =
      storage_wiper::WipeStorage(*std::move(fvm_device), config_);
  if (blobfs.is_error()) {
    FX_LOGS(ERROR) << "WipeStorage failed: " << blobfs.status_string();
    completer.ReplyError(blobfs.error_value());
    return;
  }

  zx::status blob_data_root = blobfs->DataRoot();
  if (blob_data_root.is_error()) {
    FX_LOGS(ERROR) << "Failed to obtain Blobfs data root: " << blob_data_root.status_string();
    completer.ReplyError(blob_data_root.error_value());
    return;
  }
  ZX_ASSERT(blob_data_root->is_valid());

  fidl::ServerEnd<fuchsia_io::Node> server_end{request->blobfs_root.TakeChannel()};

  if (const auto clone_result =
          fidl::WireCall(blob_data_root.value())
              ->Clone(fuchsia_io::OpenFlags::kCloneSameRights, std::move(server_end));
      !clone_result.ok()) {
    FX_LOGS(ERROR) << "Failed to Clone Blobfs data root: " << clone_result.status_string();
    completer.ReplyError(clone_result.status());
    return;
  }

  completer.ReplySuccess();

  // Release Blobfs handle so it doesn't get shutdown when the |blobfs| variable goes out of scope.
  // Blobfs will be shutdown when the fshost component collection is shutdown by component manager.
  blobfs->Release();
}

}  // namespace fshost
