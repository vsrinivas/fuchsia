// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filesystem-mounter.h"

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.fxfs/cpp/wire.h>
#include <fidl/fuchsia.fxfs/cpp/wire_types.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_types.h>
#include <fidl/fuchsia.update.verify/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>
#include <sys/stat.h>
#include <zircon/status.h>

#include <string>

#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include "constants.h"
#include "fdio.h"
#include "lib/async/cpp/task.h"
#include "lib/fdio/fd.h"
#include "lib/fdio/namespace.h"
#include "lib/fidl/cpp/wire/channel.h"
#include "lib/fidl/cpp/wire/internal/transport_channel.h"
#include "lib/zx/result.h"
#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/launch.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/lib/storage/fs_management/cpp/options.h"
#include "src/storage/blobfs/mount.h"
#include "src/storage/fshost/fs-manager.h"
#include "src/storage/fshost/fxfs.h"
#include "src/storage/fshost/utils.h"

namespace fshost {

namespace fio = fuchsia_io;

namespace {

zx::result<> CopyDataToFilesystem(fidl::ClientEnd<fuchsia_io::Directory> data_root, Copier copier) {
  fbl::unique_fd fd;
  if (zx_status_t status =
          fdio_fd_create(data_root.TakeHandle().release(), fd.reset_and_get_address());
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::make_result(copier.Write(std::move(fd)));
}

zx::result<std::string> GetDevicePath(const zx::channel& block_device) {
  std::string device_path;
  if (auto result =
          fidl::WireCall(fidl::UnownedClientEnd<fuchsia_device::Controller>(block_device.borrow()))
              ->GetTopologicalPath();
      result.status() != ZX_OK) {
    FX_LOGS(WARNING) << "Unable to get device topological path (FIDL error): "
                     << zx_status_get_string(result.status());
    return zx::error(result.status());
  } else if (result->is_error()) {
    FX_LOGS(WARNING) << "Unable to get device topological path: "
                     << zx_status_get_string(result->error_value());
    return zx::error(result->error_value());
  } else {
    device_path = result->value()->path.get();
  }
  return zx::ok(device_path);
}

}  // namespace

StartedFilesystem::StartedFilesystem(fs_management::StartedSingleVolumeFilesystem&& fs)
    : fs_(std::move(fs)) {}
StartedFilesystem::StartedFilesystem(fs_management::StartedMultiVolumeFilesystem&& fs)
    : fs_(std::move(fs)) {}

void StartedFilesystem::Detach() {
  std::visit([&](auto& fs) { fs.Release(); }, fs_);
}

zx::result<StartedFilesystem> LaunchFilesystem(zx::channel block_device,
                                               const fs_management::MountOptions& options,
                                               fs_management::DiskFormat format) {
  fbl::unique_fd device_fd;
  if (auto status = fdio_fd_create(block_device.release(), device_fd.reset_and_get_address());
      status != ZX_OK) {
    return zx::error(status);
  }

  if (format == fs_management::kDiskFormatFxfs) {
    auto fs = fs_management::MountMultiVolume(std::move(device_fd), format, options,
                                              fs_management::LaunchLogsAsync);
    if (fs.is_error())
      return fs.take_error();
    return zx::ok(StartedFilesystem(std::move(*fs)));
  }
  auto fs =
      fs_management::Mount(std::move(device_fd), format, options, fs_management::LaunchLogsAsync);
  if (fs.is_error())
    return fs.take_error();
  return zx::ok(StartedFilesystem(std::move(*fs)));
}

zx::result<> FilesystemMounter::MountLegacyFilesystem(FsManager::MountPoint point,
                                                      fs_management::DiskFormat df,
                                                      const char* binary_path,
                                                      const fs_management::MountOptions& options,
                                                      zx::channel block_device) const {
  std::string device_path = GetDevicePath(block_device).value_or("");
  std::optional endpoints_or = fshost_.TakeMountPointServerEnd(point, true);
  if (!endpoints_or.has_value()) {
    FX_LOGS(ERROR) << "Failed to take mountpoint server end";
    return zx::error(ZX_ERR_BAD_STATE);
  }
  auto [export_root, server_end] = std::move(endpoints_or.value());
  FX_LOGS(INFO) << "Mounting device " << device_path << " with " << binary_path << " at "
                << FsManager::MountPointPath(FsManager::MountPoint::kData);
  if (auto status =
          LaunchFsNative(std::move(server_end), binary_path, std::move(block_device), options);
      status.is_error()) {
    FX_PLOGS(ERROR, status.status_value()) << "Failed to launch filesystem";
    return status.take_error();
  }
  fshost_.RegisterDevicePath(FsManager::MountPoint::kData, device_path);

  if (zx_status_t status = fshost_.ForwardFsDiagnosticsDirectory(
          FsManager::MountPoint::kData, fs_management::DiskFormatString(df));
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to add diagnostic directory for "
                            << fs_management::DiskFormatString(df);
  }
  return zx::ok();
}

zx::result<StartedFilesystem> FilesystemMounter::LaunchFs(
    zx::channel block_device, const fs_management::MountOptions& options,
    fs_management::DiskFormat format) const {
  return LaunchFilesystem(std::move(block_device), options, format);
}

zx::result<> FilesystemMounter::LaunchFsNative(fidl::ServerEnd<fuchsia_io::Directory> server,
                                               const char* binary, zx::channel block_device_client,
                                               const fs_management::MountOptions& options) const {
  FX_LOGS(INFO) << "FilesystemMounter::LaunchFsNative(" << binary << ")";
  size_t num_handles = 2;
  zx_handle_t handles[] = {server.TakeChannel().release(), block_device_client.release()};
  uint32_t ids[] = {PA_DIRECTORY_REQUEST, FS_HANDLE_BLOCK_DEVICE_ID};

  fbl::Vector<const char*> argv;
  argv.push_back(binary);
  if (options.readonly) {
    argv.push_back("--readonly");
  }
  if (options.verbose_mount) {
    argv.push_back("--verbose");
  }
  if (options.write_compression_algorithm) {
    argv.push_back("--compression");
    argv.push_back(options.write_compression_algorithm->c_str());
  }
  if (options.sandbox_decompression) {
    argv.push_back("--sandbox_decompression");
  }
  if (options.cache_eviction_policy) {
    argv.push_back("--eviction_policy");
    argv.push_back(options.cache_eviction_policy->c_str());
  }
  argv.push_back("mount");
  argv.push_back(nullptr);
  if (zx_status_t status =
          Launch(*zx::job::default_job(), argv[0], argv.data(), nullptr, -1,
                 /* TODO(fxbug.dev/32044) */ zx::resource(), handles, ids, num_handles, nullptr);
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok();
}

zx_status_t FilesystemMounter::MountData(zx::channel block_device, std::optional<Copier> copier,
                                         fs_management::MountOptions options,
                                         fs_management::DiskFormat format) {
  if (data_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (DiskFormatComponentUrl(format).empty()) {
    // Some filesystems aren't set up to run as a component yet.
    // TODO(fxbug.dev/91577): Remove this special case.
    const std::string binary_path = fs_management::DiskFormatBinaryPath(format);
    ZX_ASSERT(!binary_path.empty());
    if (copier) {
      FX_LOGS(INFO) << "Copying data into filesystem...";
      auto device = CloneNode(fidl::UnownedClientEnd<fuchsia_io::Node>(block_device.borrow()));
      if (device.is_error()) {
        FX_PLOGS(WARNING, device.status_value())
            << "Failed to clone block device for copying; expect data loss";
      } else if (zx_status_t status =
                     CopyDataToLegacyFilesystem(format, std::move(*device), *copier);
                 status != ZX_OK) {
        FX_PLOGS(WARNING, status) << "Failed to copy data; expect data loss";
      } else {
        FX_LOGS(INFO) << "Copying successful!";
      }
    }
    if (auto status = MountLegacyFilesystem(FsManager::MountPoint::kData, format,
                                            binary_path.c_str(), options, std::move(block_device));
        status.is_error()) {
      FX_PLOGS(ERROR, status.status_value()) << "Failed to mount filesystem";
      return status.status_value();
    }
  } else {
    // Note: filesystem-mounter-test.cc stubs out LaunchFs and passes in invalid channels.
    // GetDevicePath and CloneNode errors are ignored and are benign in these tests.
    const std::string device_path =
        GetDevicePath(fidl::UnownedClientEnd<fuchsia_device::Controller>(block_device.borrow()))
            .value_or("");
    auto cloned = CloneNode(fidl::UnownedClientEnd<fuchsia_io::Node>(block_device.borrow()))
                      .value_or(zx::channel());

    options.component_child_name = fs_management::DiskFormatString(format);
    auto mounted_filesystem = LaunchFs(std::move(cloned), options, format);
    if (mounted_filesystem.is_error()) {
      FX_PLOGS(ERROR, mounted_filesystem.error_value()) << "Failed to launch filesystem component";
      return mounted_filesystem.error_value();
    }

    std::optional<fidl::UnownedClientEnd<fuchsia_io::Directory>> export_root;
    zx::result<fidl::ClientEnd<fuchsia_io::Directory>> data_root;
    if (const auto* fs =
            std::get_if<fs_management::StartedSingleVolumeFilesystem>(&mounted_filesystem->fs_)) {
      export_root = fs->ExportRoot();
      data_root = fs->DataRoot();
    } else if (auto* fs = std::get_if<fs_management::StartedMultiVolumeFilesystem>(
                   &mounted_filesystem->fs_)) {
      auto data_volume = UnwrapDataVolume(*fs, config_);
      if (data_volume.is_error()) {
        FX_PLOGS(ERROR, data_volume.status_value())
            << "Failed to open data volume; assuming corruption and re-initializing";
        // TODO(fxbug.dev/102666): We need to ensure the hardware key source is also wiped.
        mounted_filesystem = zx::error(ZX_ERR_INTERNAL);
        fs_management::MkfsOptions mkfs_options;
        mkfs_options.component_child_name = options.component_child_name;
        mkfs_options.component_collection_name = options.component_collection_name;
        mkfs_options.component_url = options.component_url;
        if (zx_status_t status = fs_management::Mkfs(device_path.c_str(), format,
                                                     fs_management::LaunchLogsAsync, mkfs_options);
            status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "Failed to re-format Fxfs following invalid state";
          return status;
        }
        mounted_filesystem = LaunchFs(std::move(block_device), options, format);
        if (mounted_filesystem.is_error()) {
          FX_PLOGS(ERROR, mounted_filesystem.error_value())
              << "Failed to relaunch filesystem component";
          return mounted_filesystem.error_value();
        }
        data_volume = InitDataVolume(*fs, config_);
        if (data_volume.is_error()) {
          FX_PLOGS(ERROR, data_volume.status_value()) << "Failed to create data volume";
          return data_volume.status_value();
        }
      }
      export_root = (*data_volume)->ExportRoot();
      data_root = (*data_volume)->DataRoot();
    } else {
      __builtin_unreachable();
    }
    if (data_root.is_error()) {
      FX_PLOGS(ERROR, data_root.status_value()) << "Failed to get data root";
    }

    if (copier) {
      // Copy data before we route the filesystem to the world.
      FX_LOGS(INFO) << "Copying data into filesystem...";
      if (auto status = CopyDataToFilesystem(*std::move(data_root), std::move(copier.value()));
          status.is_error()) {
        FX_PLOGS(WARNING, status.status_value()) << "Faile to copy data; expect data loss";
      } else {
        FX_LOGS(INFO) << "Copying successful!";
      }
    }

    if (zx_status_t status = RouteData(*export_root, device_path); status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to route data";
      return status;
    }

    // Detach to avoid RAII shutting the filesystem down when it goes out of scope.
    mounted_filesystem->Detach();
  }

  // Obtain data root used for serving disk usage statistics. Must be valid otherwise the lazy
  // node serving the statistics will hang indefinitely. If we fail to get the data root, just log
  // loudly, but don't fail. Serving stats is best-effort and shouldn't cause mounting to fail.
  auto data_root_or = manager().GetRoot(FsManager::MountPoint::kData);
  if (data_root_or.is_ok()) {
    inspect_manager().ServeStats("data", std::move(data_root_or.value()));
  } else {
    FX_LOGS(WARNING) << "failed to get data root to serve inspect stats. Assuming test environment "
                        "and continuing.";
  }

  data_mounted_ = true;
  return ZX_OK;
}

zx_status_t FilesystemMounter::RouteData(fidl::UnownedClientEnd<fuchsia_io::Directory> export_root,
                                         std::string_view device_path) {
  auto endpoints = manager().TakeMountPointServerEnd(FsManager::MountPoint::kData, false);
  fidl::ServerEnd<::fuchsia_io::Node> server_end(endpoints->server_end.TakeChannel());
  auto clone_or = fidl::WireCall(export_root)
                      ->Clone(fio::wire::OpenFlags::kCloneSameRights, std::move(server_end));

  if (clone_or.status() != ZX_OK) {
    FX_PLOGS(ERROR, clone_or.status()) << "Failed to route mounted filesystem to /data";
    return clone_or.status();
  }
  fshost_.RegisterDevicePath(FsManager::MountPoint::kData, device_path);

  return ZX_OK;
}

zx_status_t FilesystemMounter::MountFactoryFs(zx::channel block_device,
                                              const fs_management::MountOptions& options) {
  if (factory_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (auto result = MountLegacyFilesystem(FsManager::MountPoint::kFactory,
                                          fs_management::DiskFormat::kDiskFormatFactoryfs,
                                          kFactoryfsPath, options, std::move(block_device));
      result.is_error()) {
    return result.error_value();
  }

  factory_mounted_ = true;
  return ZX_OK;
}

zx_status_t FilesystemMounter::MountDurable(zx::channel block_device,
                                            const fs_management::MountOptions& options) {
  if (durable_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (auto result = MountLegacyFilesystem(FsManager::MountPoint::kDurable,
                                          fs_management::DiskFormat::kDiskFormatMinfs, kMinfsPath,
                                          options, std::move(block_device));
      result.is_error()) {
    return result.error_value();
  }

  durable_mounted_ = true;
  return ZX_OK;
}

zx_status_t FilesystemMounter::MountBlob(zx::channel block_device,
                                         const fs_management::MountOptions& options) {
  if (blob_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  zx::result ret = LaunchFs(std::move(block_device), options, fs_management::kDiskFormatBlobfs);
  if (ret.is_error()) {
    return ret.error_value();
  }
  // Detach to avoid RAII shutting it down when mounted_filesystem_or goes out of scope.
  ret->Detach();

  blob_mounted_ = true;
  return ZX_OK;
}

void FilesystemMounter::ReportPartitionCorrupted(fs_management::DiskFormat format) {
  fshost_.inspect_manager().LogCorruption(format);
  // Currently the only reason we report a partition as being corrupt is if it fails fsck.
  // This may need to change in the future should we want to file synthetic crash reports for
  // other possible failure modes.
  fshost_.FileReport(format, FsManager::ReportReason::kFsckFailure);
}

// This copies source data for filesystems that aren't components.
zx_status_t FilesystemMounter::CopyDataToLegacyFilesystem(fs_management::DiskFormat df,
                                                          zx::channel block_device,
                                                          const Copier& copier) const {
  FX_LOGS(INFO) << "Copying data...";
  auto export_root_or = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (export_root_or.is_error())
    return export_root_or.error_value();

  fs_management::MountOptions options;
  options.readonly = true;
  auto res = LaunchFs(std::move(block_device), options, df);
  if (res.is_error()) {
    FX_PLOGS(ERROR, res.status_value()) << "Unable to mount for copying";
    return res.status_value();
  }
  zx::result<fidl::ClientEnd<fuchsia_io::Directory>> data_root;
  if (const auto* fs = std::get_if<fs_management::StartedSingleVolumeFilesystem>(&res->fs_)) {
    data_root = fs->DataRoot();
  } else {
    FX_LOGS(ERROR) << "Unexpectedly multi-volume filesystem";
    return ZX_ERR_BAD_STATE;
  }
  if (data_root.is_error()) {
    FX_PLOGS(ERROR, data_root.status_value()) << "Unable to open data root for copying";
    return data_root.status_value();
  }
  fbl::unique_fd fd;
  if (zx_status_t status =
          fdio_fd_create(data_root->TakeChannel().release(), fd.reset_and_get_address());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "fdio_fd_create failed";
    return status;
  }
  if (zx_status_t status = copier.Write(std::move(fd)); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to copy data: " << zx_status_get_string(status);
    return status;
  }
  FX_LOGS(INFO) << "Successfully copied data";
  return ZX_OK;
}

std::string_view BinaryPathForFormat(fs_management::DiskFormat format) {
  switch (format) {
    case fs_management::kDiskFormatFxfs:
      return kFxfsPath;
    case fs_management::kDiskFormatF2fs:
      return kF2fsPath;
    case fs_management::kDiskFormatMinfs:
      return kMinfsPath;
    default:
      return "";
  }
}

}  // namespace fshost
