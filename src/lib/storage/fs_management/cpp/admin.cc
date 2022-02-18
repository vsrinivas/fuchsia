// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/admin.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/vfs.h>
#include <lib/fit/defer.h>
#include <lib/zx/channel.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <array>
#include <vector>

#include <fbl/vector.h>

#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/lib/storage/fs_management/cpp/path.h"
#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

namespace fs_management {
namespace {

using fuchsia_io::Directory;

zx::status<fidl::ClientEnd<Directory>> InitNativeFs(const char* binary, zx::channel device,
                                                    const InitOptions& options,
                                                    zx::channel crypt_client) {
  zx_status_t status;
  auto outgoing_directory_or = fidl::CreateEndpoints<Directory>();
  if (outgoing_directory_or.is_error())
    return outgoing_directory_or.take_error();
  std::array<zx_handle_t, 3> handles = {device.release(),
                                        outgoing_directory_or->server.TakeChannel().release(),
                                        crypt_client.release()};
  std::array<uint32_t, 3> ids = {FS_HANDLE_BLOCK_DEVICE_ID, PA_DIRECTORY_REQUEST,
                                 PA_HND(PA_USER0, 2)};

  // |compression_level| should outlive |argv|.
  std::string compression_level;
  std::vector<const char*> argv;
  argv.push_back(binary);
  if (options.verbose_mount) {
    argv.push_back("--verbose");
  }

  argv.push_back("mount");

  if (options.readonly) {
    argv.push_back("--readonly");
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
  argv.push_back(nullptr);
  int argc = static_cast<int>(argv.size() - 1);

  if ((status = options.callback(argc, argv.data(), handles.data(), ids.data(),
                                 handles[2] == ZX_HANDLE_INVALID ? 2 : 3)) != ZX_OK) {
    return zx::error(status);
  }

  auto cleanup = fit::defer([&outgoing_directory_or]() {
    [[maybe_unused]] auto result = Shutdown(outgoing_directory_or->client);
  });

  if (options.wait_until_ready) {
    // Wait until the filesystem is ready to take incoming requests
    auto result = fidl::WireCall(outgoing_directory_or->client)->Describe();
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
  return zx::ok(std::move(outgoing_directory_or->client));
}

}  // namespace

__EXPORT
zx::status<fidl::ClientEnd<Directory>> FsRootHandle(fidl::UnownedClientEnd<Directory> export_root,
                                                    uint32_t flags) {
  zx::channel root_client, root_server;
  auto status = zx::make_status(zx::channel::create(0, &root_client, &root_server));
  if (status.is_error()) {
    return status.take_error();
  }

  auto resp = fidl::WireCall<Directory>(export_root)
                  ->Open(flags, 0, fidl::StringView("root"), std::move(root_server));
  if (!resp.ok()) {
    return zx::error(resp.status());
  }

  return zx::ok(fidl::ClientEnd<Directory>(std::move(root_client)));
}

__EXPORT
zx::status<fidl::ClientEnd<Directory>> FsInit(zx::channel device, DiskFormat df,
                                              const InitOptions& options,
                                              zx::channel crypt_client) {
  switch (df) {
    case kDiskFormatMinfs:
      return InitNativeFs(GetBinaryPath("minfs").c_str(), std::move(device), options,
                          std::move(crypt_client));
    case kDiskFormatFxfs:
      return InitNativeFs(GetBinaryPath("fxfs").c_str(), std::move(device), options,
                          std::move(crypt_client));
    case kDiskFormatBlobfs:
      return InitNativeFs(GetBinaryPath("blobfs").c_str(), std::move(device), options,
                          std::move(crypt_client));
    case kDiskFormatFat:
      // For now, fatfs will only ever be in a package and never in /boot/bin, so we can hard-code
      // the path.
      return InitNativeFs("/pkg/bin/fatfs", std::move(device), options, std::move(crypt_client));
    case kDiskFormatFactoryfs:
      return InitNativeFs(GetBinaryPath("factoryfs").c_str(), std::move(device), options,
                          std::move(crypt_client));
    case kDiskFormatF2fs:
      return InitNativeFs(GetBinaryPath("f2fs").c_str(), std::move(device), options,
                          std::move(crypt_client));
    default:
      auto* format = CustomDiskFormat::Get(df);
      if (format == nullptr) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
      return InitNativeFs(format->binary_path().c_str(), std::move(device), options,
                          std::move(crypt_client));
  }
}

}  // namespace fs_management
