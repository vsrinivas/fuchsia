// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "admin.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/vfs.h>
#include <lib/fit/defer.h>
#include <lib/zx/channel.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <array>
#include <vector>

#include <fbl/vector.h>
#include <fs-management/admin.h>

#include "path.h"
#include "src/lib/storage/vfs/cpp/vfs.h"

namespace fio = fuchsia_io;

namespace fs_management {
namespace {

void UnmountHandle(zx_handle_t export_root, bool wait_until_ready) {
  zx::channel root;
  fs_root_handle(export_root, root.reset_and_get_address());
  fs::Vfs::UnmountHandle(std::move(root), wait_until_ready ? zx::time::infinite() : zx::time(0));
}

zx::status<> InitNativeFs(const char* binary, zx::channel device, const InitOptions& options,
                          OutgoingDirectory outgoing_directory) {
  zx_status_t status;
  constexpr size_t kNumHandles = 2;
  std::array<zx_handle_t, kNumHandles> handles = {device.release(),
                                                  outgoing_directory.server.release()};
  std::array<uint32_t, kNumHandles> ids = {FS_HANDLE_BLOCK_DEVICE_ID, PA_DIRECTORY_REQUEST};

  // |compression_level| should outlive |argv|.
  std::string compression_level;
  std::vector<const char*> argv;
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
  if (options.write_compression_algorithm) {
    argv.push_back("--compression");
    argv.push_back(options.write_compression_algorithm);
  }
  if (options.write_compression_level >= 0) {
    compression_level = std::to_string(options.write_compression_level);
    argv.push_back("--compression_level");
    argv.push_back(compression_level.c_str());
  }
  if (options.cache_eviction_policy) {
    argv.push_back("--eviction_policy");
    argv.push_back(options.cache_eviction_policy);
  }
  if (options.fsck_after_every_transaction) {
    argv.push_back("--fsck_after_every_transaction");
  }
  if (options.sandbox_decompression) {
    argv.push_back("--sandbox_decompression");
  }
  argv.push_back("mount");
  argv.push_back(nullptr);
  int argc = static_cast<int>(argv.size() - 1);

  auto cleanup = fit::defer([&outgoing_directory, &options]() {
    UnmountHandle(outgoing_directory.client->get(), options.wait_until_ready);
  });

  if ((status = options.callback(argc, argv.data(), handles.data(), ids.data(), kNumHandles)) !=
      ZX_OK) {
    return zx::error(status);
  }

  if (options.wait_until_ready) {
    // Wait until the filesystem is ready to take incoming requests
    auto result = fidl::WireCall<fio::Node>(outgoing_directory.client).Describe();
    switch (result.status()) {
      case ZX_OK:
        break;
      case ZX_ERR_PEER_CLOSED:
        return zx::error(ZX_ERR_BAD_STATE);
      default:
        return zx::error(result.status());
    }
  }
  cleanup.cancel();
  return zx::ok();
}

}  // namespace

zx::status<zx::channel> GetFsRootHandle(zx::unowned_channel export_root, uint32_t flags) {
  zx::channel root_client, root_server;
  auto status = zx::make_status(zx::channel::create(0, &root_client, &root_server));
  if (status.is_error()) {
    return status.take_error();
  }

  auto resp = fidl::WireCall<fio::Directory>(zx::unowned_channel(export_root))
                  .Open(flags, 0, fidl::StringView("root"), std::move(root_server));
  if (!resp.ok()) {
    return zx::error(resp.status());
  }

  return zx::ok(std::move(root_client));
}

zx::status<> FsInit(zx::channel device, disk_format_t df, const InitOptions& options,
                    OutgoingDirectory outgoing_directory) {
  switch (df) {
    case DISK_FORMAT_MINFS:
      return InitNativeFs(fs_management::GetBinaryPath("minfs").c_str(), std::move(device), options,
                          std::move(outgoing_directory));
    case DISK_FORMAT_FXFS:
      return InitNativeFs(fs_management::GetBinaryPath("fxfs").c_str(), std::move(device), options,
                          std::move(outgoing_directory));
    case DISK_FORMAT_BLOBFS:
      return InitNativeFs(fs_management::GetBinaryPath("blobfs").c_str(), std::move(device),
                          options, std::move(outgoing_directory));
    case DISK_FORMAT_FAT:
      // For now, fatfs will only ever be in a package and never in /boot/bin, so we can hard-code
      // the path.
      return InitNativeFs("/pkg/bin/fatfs", std::move(device), options,
                          std::move(outgoing_directory));
    case DISK_FORMAT_FACTORYFS:
      return InitNativeFs(fs_management::GetBinaryPath("factoryfs").c_str(), std::move(device),
                          options, std::move(outgoing_directory));
    default:
      auto* format = CustomDiskFormat::Get(df);
      if (format == nullptr) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
      return InitNativeFs(format->binary_path().c_str(), std::move(device), options,
                          std::move(outgoing_directory));
  }
}

}  // namespace fs_management

__EXPORT
zx_status_t fs_init(zx_handle_t device_handle, disk_format_t df, const InitOptions& options,
                    zx_handle_t* out_export_root) {
  fs_management::OutgoingDirectory handles;
  zx::channel client;
  zx_status_t status = zx::channel::create(0, &client, &handles.server);
  if (status != ZX_OK)
    return status;
  handles.client = zx::unowned_channel(client);
  status = FsInit(zx::channel(device_handle), df, options, std::move(handles)).status_value();
  if (status != ZX_OK)
    return status;
  *out_export_root = client.release();
  return ZX_OK;
}

__EXPORT
zx_status_t fs_root_handle(zx_handle_t export_root, zx_handle_t* out_root) {
  // The POSIX flag here requests that the old connection rights be inherited by the new connection.
  // This specifically ensures that WRITABLE connections continue to have the WRITABLE right, while
  // read-only connections do not.
  auto handle_or = fs_management::GetFsRootHandle(
      zx::unowned_channel(export_root),
      fio::wire::kOpenRightReadable | fio::wire::kOpenFlagPosix | fio::wire::kOpenRightAdmin);
  if (handle_or.is_error())
    return handle_or.status_value();
  *out_root = handle_or.value().release();
  return ZX_OK;
}
