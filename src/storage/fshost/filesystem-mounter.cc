// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filesystem-mounter.h"

#include <dirent.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_types.h>
#include <fidl/fuchsia.update.verify/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>
#include <sys/stat.h>
#include <zircon/status.h>

#include <fbl/ref_ptr.h>

#include "constants.h"
#include "fdio.h"
#include "fshost-fs-provider.h"
#include "pkgfs-launcher.h"
#include "src/storage/blobfs/mount.h"
#include "src/storage/minfs/minfs.h"

namespace fshost {

namespace fio = fuchsia_io;

zx::status<> FilesystemMounter::LaunchFsComponent(zx::channel block_device,
                                                  fuchsia_fs_startup::wire::StartOptions options,
                                                  const std::string& fs_name) {
  std::string startup_service_path = std::string("/") + fs_name + "/fuchsia.fs.startup.Startup";
  auto startup_client_end =
      service::Connect<fuchsia_fs_startup::Startup>(startup_service_path.c_str());
  if (startup_client_end.is_error()) {
    FX_LOGS(ERROR) << "failed to connect to startup service at " << startup_service_path << ": "
                   << startup_client_end.status_string();
    return startup_client_end.take_error();
  }
  auto startup_client = fidl::BindSyncClient(std::move(*startup_client_end));
  fidl::ClientEnd<fuchsia_hardware_block::Block> block_client_end(std::move(block_device));
  auto startup_res = startup_client->Start(std::move(block_client_end), std::move(options));
  if (!startup_res.ok()) {
    FX_LOGS(ERROR) << "failed to start through startup service at " << startup_service_path << ": "
                   << startup_res.status_string();
    return zx::make_status(startup_res.status());
  }
  FX_LOGS(INFO) << "successfully mounted " << fs_name;
  return zx::ok();
}

zx_status_t FilesystemMounter::LaunchFs(int argc, const char** argv, zx_handle_t* hnd,
                                        uint32_t* ids, size_t len, uint32_t fs_flags) {
  FshostFsProvider fs_provider;
  DevmgrLauncher launcher(&fs_provider);
  return launcher.Launch(*zx::job::default_job(), argv[0], argv, nullptr, -1,
                         /* TODO(fxbug.dev/32044) */ zx::resource(), hnd, ids, len, nullptr,
                         fs_flags);
}

zx::status<> FilesystemMounter::MountFilesystem(FsManager::MountPoint point, const char* binary,
                                                const fs_management::MountOptions& options,
                                                zx::channel block_device_client, uint32_t fs_flags,
                                                fidl::ClientEnd<fuchsia_fxfs::Crypt> crypt_client) {
  std::string device_path;
  if (auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_device::Controller>(
                                       block_device_client.borrow()))
                        ->GetTopologicalPath();
      result.status() != ZX_OK) {
    FX_LOGS(WARNING) << "Unable to get device topological path (FIDL error): "
                     << zx_status_get_string(result.status());
  } else if (result->result.is_err()) {
    FX_LOGS(WARNING) << "Unable to get device topological path: "
                     << zx_status_get_string(result->result.err());
  } else {
    device_path = result->result.response().path.get();
  }

  zx::status create_endpoints = fidl::CreateEndpoints<fio::Node>();
  if (create_endpoints.is_error()) {
    return create_endpoints.take_error();
  }
  auto [export_root, server_end] = std::move(create_endpoints.value());

  size_t num_handles = crypt_client ? 3 : 2;
  zx_handle_t handles[] = {server_end.TakeChannel().release(), block_device_client.release(),
                           crypt_client.TakeChannel().release()};
  uint32_t ids[] = {PA_DIRECTORY_REQUEST, FS_HANDLE_BLOCK_DEVICE_ID, PA_HND(PA_USER0, 2)};

  fbl::Vector<const char*> argv;
  argv.push_back(binary);
  if (options.readonly) {
    argv.push_back("--readonly");
  }
  if (options.verbose_mount) {
    argv.push_back("--verbose");
  }
  if (options.collect_metrics) {
    argv.push_back("--metrics");
  }
  if (options.write_compression_algorithm != nullptr) {
    argv.push_back("--compression");
    argv.push_back(options.write_compression_algorithm);
  }
  if (options.sandbox_decompression) {
    argv.push_back("--sandbox_decompression");
  }
  if (options.cache_eviction_policy != nullptr) {
    argv.push_back("--eviction_policy");
    argv.push_back(options.cache_eviction_policy);
  }
  argv.push_back("mount");
  argv.push_back(nullptr);
  if (zx_status_t status = LaunchFs(static_cast<int>(argv.size() - 1), argv.data(), handles, ids,
                                    num_handles, fs_flags);
      status != ZX_OK) {
    return zx::error(status);
  }

  auto result = fidl::WireCall(export_root)->Describe();
  if (!result.ok()) {
    return zx::error(result.status());
  }

  zx::channel root_client, root_server;
  if (zx_status_t status = zx::channel::create(0, &root_client, &root_server); status != ZX_OK) {
    return zx::error(status);
  }

  if (auto resp = fidl::WireCall<fio::Directory>(zx::unowned_channel(export_root.channel()))
                      ->Open(fio::wire::kOpenRightReadable | fio::wire::kOpenFlagPosixWritable |
                                 fio::wire::kOpenFlagPosixExecutable,
                             0, fidl::StringView("root"), std::move(root_server));
      !resp.ok()) {
    return zx::error(resp.status());
  }

  return InstallFs(point, device_path, export_root.TakeChannel(), std::move(root_client));
}

zx_status_t FilesystemMounter::MountData(zx::channel block_device,
                                         const fs_management::MountOptions& options,
                                         fs_management::DiskFormat format) {
  if (data_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  fidl::ClientEnd<fuchsia_fxfs::Crypt> crypt_client;
  std::string binary_path = config_.ReadStringOptionValue(Config::kDataFilesystemBinaryPath);
  // Config overrides passed in format.
  if (binary_path.empty()) {
    switch (format) {
      case fs_management::kDiskFormatFxfs: {
        binary_path = kFxfsPath;
        break;
      }
      case fs_management::kDiskFormatMinfs: {
        binary_path = kMinfsPath;
        break;
      }
      case fs_management::kDiskFormatF2fs: {
        binary_path = kF2fsPath;
        break;
      }
      default: {
        FX_LOGS(INFO) << "Device format '" << fs_management::DiskFormatString(format)
                      << "'. Defaulting to minfs.";
        format = fs_management::kDiskFormatMinfs;
        binary_path = kMinfsPath;
      }
    }
  }

  if (binary_path == kFxfsPath) {
    auto crypt_client_or = GetCryptClient();
    if (crypt_client_or.is_error()) {
      return crypt_client_or.error_value();
    }
    crypt_client = crypt_client_or->TakeHandle();
  }

  if (auto result = MountFilesystem(FsManager::MountPoint::kData, binary_path.c_str(), options,
                                    std::move(block_device), FS_SVC, std::move(crypt_client));
      result.is_error()) {
    return result.error_value();
  }

  if (zx_status_t status = fshost_.ForwardFsDiagnosticsDirectory(
          FsManager::MountPoint::kData, fs_management::DiskFormatString(format));
      status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to add diagnostic directory for "
                   << fs_management::DiskFormatString(format) << ": "
                   << zx_status_get_string(status);
  }

  data_mounted_ = true;
  return ZX_OK;
}

zx_status_t FilesystemMounter::MountInstall(zx::channel block_device,
                                            const fs_management::MountOptions& options) {
  if (install_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (auto result = MountFilesystem(FsManager::MountPoint::kInstall, kMinfsPath, options,
                                    std::move(block_device), FS_SVC);
      result.is_error()) {
    return result.error_value();
  }

  install_mounted_ = true;
  return ZX_OK;
}

zx_status_t FilesystemMounter::MountFactoryFs(zx::channel block_device,
                                              const fs_management::MountOptions& options) {
  if (factory_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (auto result = MountFilesystem(FsManager::MountPoint::kFactory, kFactoryfsPath, options,
                                    std::move(block_device), FS_SVC);
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

  if (auto result = MountFilesystem(FsManager::MountPoint::kDurable, kMinfsPath, options,
                                    std::move(block_device), FS_SVC);
      result.is_error()) {
    return result.error_value();
  }

  durable_mounted_ = true;
  return ZX_OK;
}

zx_status_t FilesystemMounter::MountBlob(zx::channel block_device,
                                         fuchsia_fs_startup::wire::StartOptions options) {
  if (blob_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  zx::status ret = LaunchFsComponent(std::move(block_device), options, "blobfs");
  if (ret.is_error()) {
    return ret.error_value();
  }

  blob_mounted_ = true;
  return ZX_OK;
}

void FilesystemMounter::TryMountPkgfs() {
  // Pkgfs waits for the following to mount before initializing:
  //   - Blobfs. Pkgfs is launched from blobfs, so this is a hard requirement.
  //   - Minfs. Pkgfs and other components want minfs to exist, so although they
  //   could launch and query for it later, this synchronization point means that
  //   subsequent clients will no longer need to query.
  //
  // TODO(fxbug.dev/38621): In the future, this mechanism may be replaced with a feed-forward
  // design to the mounted filesystems.
  if (!pkgfs_mounted_ && blob_mounted_ && (data_mounted_ || !WaitForData())) {
    // Historically we don't retry if pkgfs fails to launch, which seems reasonable since the
    // cause of a launch failure is unlikely to be transient.
    // TODO(fxbug.dev/58363): fshost should handle failures to mount critical filesystems better.
    auto status = LaunchPkgfs(this);
    if (status.is_error()) {
      FX_LOGS(ERROR) << "failed to launch pkgfs: " << status.status_string();
    }
    pkgfs_mounted_ = true;
  }
}

void FilesystemMounter::ReportMinfsCorruption() {
  fshost_.mutable_metrics()->LogMinfsCorruption();
  fshost_.FlushMetrics();
  fshost_.FileReport(FsManager::ReportReason::kMinfsCorrupted);
}

zx::status<fidl::ClientEnd<fuchsia_fxfs::Crypt>> FilesystemMounter::GetCryptClient() {
  auto crypt_endpoints_or = fidl::CreateEndpoints<fuchsia_fxfs::Crypt>();
  if (crypt_endpoints_or.is_error())
    return zx::error(crypt_endpoints_or.status_value());
  if (zx_status_t status =
          fdio_service_connect(fidl::DiscoverableProtocolDefaultPath<fuchsia_fxfs::Crypt>,
                               crypt_endpoints_or->server.TakeChannel().release());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to connect to crypt service";
    return zx::error(status);
  }
  return zx::ok(std::move(crypt_endpoints_or->client));
}

}  // namespace fshost
