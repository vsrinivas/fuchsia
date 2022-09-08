// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/admin-server.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/markers.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/markers.h>
#include <lib/async/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fd.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/hw/gpt.h>

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

namespace fshost {

fbl::RefPtr<fs::Service> AdminServer::Create(FsManager* fs_manager,
                                             const fshost_config::Config& config,
                                             async_dispatcher* dispatcher) {
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, fs_manager, config](fidl::ServerEnd<fuchsia_fshost::Admin> chan) {
        zx_status_t status = fidl::BindSingleInFlightOnly(
            dispatcher, std::move(chan), std::make_unique<AdminServer>(fs_manager, config));
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
  if (!config_.fvm_ramdisk()) {
    FX_LOGS(INFO) << "Can't WriteDataFile from a non-recovery build; fvm_ramdisk must be set.";
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }
  if (!files::IsValidCanonicalPath(request->filename.get())) {
    FX_LOGS(WARNING) << "Bad path " << request->filename.get();
    completer.ReplyError(ZX_ERR_BAD_PATH);
    return;
  }

  uint64_t content_size = 0;
  if (zx_status_t status = request->payload.get_prop_content_size(&content_size); status != ZX_OK) {
    if (status = request->payload.get_size(&content_size); status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
  }
  ssize_t content_ssize;
  if (!safemath::MakeCheckedNum<size_t>(content_size)
           .Cast<ssize_t>()
           .AssignIfValid(&content_ssize)) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  ZX_DEBUG_ASSERT(!config_.ramdisk_prefix().empty());
  const fs_management::PartitionMatcher fvm_matcher{
      .detected_disk_format = fs_management::kDiskFormatFvm,
      .ignore_prefix = config_.ramdisk_prefix(),
  };
  auto fvm = OpenPartition(&fvm_matcher, ZX_SEC(5), nullptr);
  if (fvm.is_error()) {
    FX_PLOGS(ERROR, fvm.status_value()) << "Failed to find FVM";
    completer.ReplyError(fvm.status_value());
    return;
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
  auto partition = OpenPartition(&data_matcher, ZX_SEC(5), nullptr);
  if (partition.is_error()) {
    FX_PLOGS(ERROR, partition.status_value()) << "Failed to find data partition";
    completer.ReplyError(partition.status_value());
    return;
  }
  FX_LOGS(INFO) << "Using data path " << GetTopologicalPath(partition->get());

  auto detected_format = fs_management::DetectDiskFormat(partition->get());
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
    partition = OpenPartition(&zxcrypt_matcher, ZX_SEC(5), nullptr);
    if (partition.is_error()) {
      FX_PLOGS(ERROR, partition.status_value()) << "Failed to find inner data partition";
      completer.ReplyError(partition.status_value());
      return;
    }
  }
  std::string partition_path = GetTopologicalPath(partition->get());
  FX_LOGS(INFO) << "Using data partition at " << partition_path << ", has format "
                << fs_management::DiskFormatString(detected_format);

  std::optional<StartedFilesystem> started_fs;
  fidl::ClientEnd<fuchsia_io::Directory> data_root;
  if (detected_format != format) {
    FX_LOGS(INFO) << "Data partition is not in expected format; reformatting";
    if (format == fs_management::kDiskFormatFxfs) {
      zx::channel block_device;
      if (zx_status_t status =
              fdio_fd_clone(partition->get(), block_device.reset_and_get_address());
          status != ZX_OK) {
        completer.ReplyError(status);
        return;
      }
      auto fxfs = FormatFxfsAndInitDataVolume(
          fidl::ClientEnd<fuchsia_hardware_block::Block>(std::move(block_device)), config_);
      if (fxfs.is_error()) {
        FX_PLOGS(ERROR, fxfs.status_value()) << "Failed to format data partition";
        completer.ReplyError(fxfs.status_value());
        return;
      }
      if (auto status = fxfs->second->DataRoot(); status.is_ok()) {
        data_root = std::move(*status);
      } else {
        FX_PLOGS(ERROR, status.status_value()) << "Failed to get data root";
        completer.ReplyError(status.status_value());
        return;
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
        completer.ReplyError(status);
        return;
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
        completer.ReplyError(fxfs.status_value());
        return;
      }
      auto data_volume = UnwrapDataVolume(*fxfs, config_);
      if (data_volume.is_error()) {
        FX_PLOGS(ERROR, data_volume.status_value()) << "Failed to unwrap data volume";
        completer.ReplyError(data_volume.status_value());
        return;
      }
      if (auto status = (*data_volume)->DataRoot(); status.is_ok()) {
        data_root = std::move(*status);
      } else {
        FX_PLOGS(ERROR, status.status_value()) << "Failed to get data root";
        completer.ReplyError(status.status_value());
        return;
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
        completer.ReplyError(fs.status_value());
        return;
      }
      if (auto status = fs->DataRoot(); status.is_ok()) {
        data_root = std::move(*status);
      } else {
        FX_PLOGS(ERROR, status.status_value()) << "Failed to get data root";
        completer.ReplyError(status.status_value());
        return;
      }
      started_fs.emplace(std::move(*fs));
    }
  }
  fbl::unique_fd root;
  if (zx_status_t status = fdio_fd_create(data_root.handle()->get(), root.reset_and_get_address());
      status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  fzl::OwnedVmoMapper mapper;
  if (zx_status_t status = mapper.Map(std::move(request->payload), content_size); status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  const std::string path(request->filename.get());
  const std::string base = files::GetDirectoryName(path);
  if (!base.empty() && !files::CreateDirectoryAt(root.get(), base)) {
    FX_LOGS(ERROR) << "Failed to create parent directory " << base;
    completer.ReplyError(ZX_ERR_IO);
  }
  if (files::WriteFileAt(root.get(), path, static_cast<const char*>(mapper.start()),
                         content_ssize)) {
    syncfs(root.get());
    completer.ReplySuccess();
  } else {
    FX_LOGS(ERROR) << "Failed to write file " << request->filename.data();
    completer.ReplyError(ZX_ERR_IO);
  }
}

}  // namespace fshost
