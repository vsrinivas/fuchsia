// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "admin.h"

#include <fuchsia/fshost/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/vfs.h>
#include <lib/zx/channel.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <array>

#include <fbl/auto_call.h>
#include <fbl/vector.h>
#include <fs-management/admin.h>
#include <fs/vfs.h>

#include "path.h"

namespace fio = ::llcpp::fuchsia::io;
namespace fshost = ::llcpp::fuchsia::fshost;

namespace fs_management {
namespace {

void UnmountHandle(zx_handle_t export_root, bool wait_until_ready) {
  zx::channel root;
  fs_root_handle(export_root, root.reset_and_get_address());
  fs::Vfs::UnmountHandle(std::move(root), wait_until_ready ? zx::time::infinite() : zx::time(0));
}

zx::status<> InitNativeFs(const char* binary, zx::channel device, const init_options_t& options,
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
  if (options.enable_journal) {
    argv.push_back("--journal");
  }
  if (options.enable_pager) {
    argv.push_back("--pager");
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
  argv.push_back("mount");
  argv.push_back(nullptr);
  int argc = static_cast<int>(argv.size() - 1);

  auto cleanup = fbl::MakeAutoCall([&outgoing_directory, &options]() {
    UnmountHandle(outgoing_directory.client->get(), options.wait_until_ready);
  });

  if ((status = options.callback(argc, argv.data(), handles.data(), ids.data(), kNumHandles)) !=
      ZX_OK) {
    return zx::error(status);
  }

  if (options.wait_until_ready) {
    // Wait until the filesystem is ready to take incoming requests
    zx_signals_t observed;
    zx_signals_t signals = ZX_USER_SIGNAL_0  // the filesystem is initialized and serving requests
                           | ZX_CHANNEL_PEER_CLOSED;  // filesystem closed the channel on error
    status = outgoing_directory.client->wait_one(signals, zx::time::infinite(), &observed);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    if (observed & ZX_CHANNEL_PEER_CLOSED) {
      return zx::error(ZX_ERR_BAD_STATE);
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

  auto resp = fio::Directory::Call::Open(zx::unowned_channel(export_root), flags, 0,
                                         fidl::StringView("root"), std::move(root_server));
  if (!resp.ok()) {
    return zx::error(resp.status());
  }

  return zx::ok(std::move(root_client));
}

zx::status<> FsInit(zx::channel device, disk_format_t df, const init_options_t& options,
                    OutgoingDirectory outgoing_directory) {
  switch (df) {
    case DISK_FORMAT_MINFS:
      return InitNativeFs(fs_management::GetBinaryPath("minfs").c_str(), std::move(device), options,
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
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}

}  // namespace fs_management

const init_options_t default_init_options = {
    .readonly = false,
    .verbose_mount = false,
    .collect_metrics = false,
    .wait_until_ready = true,
    .enable_journal = true,
    .enable_pager = false,
    .write_compression_algorithm = nullptr,
    .write_compression_level = -1,
    .cache_eviction_policy = nullptr,
    .fsck_after_every_transaction = false,
    .callback = launch_stdio_async,
};

__EXPORT
zx_status_t fs_init(zx_handle_t device_handle, disk_format_t df, const init_options_t* options,
                    zx_handle_t* out_export_root) {
  fs_management::OutgoingDirectory handles;
  zx::channel client;
  zx_status_t status = zx::channel::create(0, &client, &handles.server);
  if (status != ZX_OK)
    return status;
  handles.client = zx::unowned_channel(client);
  status = FsInit(zx::channel(device_handle), df, *options, std::move(handles)).status_value();
  if (status != ZX_OK)
    return status;
  *out_export_root = client.release();
  return ZX_OK;
}

__EXPORT
zx_status_t fs_register(zx_handle_t export_root) {
  zx_status_t status;

  zx::channel export_client, export_server;
  if ((status = zx::channel::create(0, &export_client, &export_server)) != ZX_OK) {
    return status;
  }

  auto clone_resp = fio::Node::Call::Clone(zx::unowned_channel(export_root),
                                           fio::CLONE_FLAG_SAME_RIGHTS, std::move(export_server));
  if (!clone_resp.ok()) {
    return clone_resp.status();
  }

  zx::channel registry_client_chan, registry_server;
  if ((status = zx::channel::create(0, &registry_client_chan, &registry_server)) != ZX_OK) {
    return status;
  }

  std::string path = std::string("/svc/") + fshost::Registry::Name;
  if ((status = fdio_service_connect(path.c_str(), registry_server.release())) != ZX_OK) {
    return status;
  }

  fshost::Registry::SyncClient registry_client(std::move(registry_client_chan));
  auto register_resp = registry_client.RegisterFilesystem(std::move(export_client));
  if (!register_resp.ok()) {
    return register_resp.status();
  }
  if (register_resp.value().s != ZX_OK) {
    return register_resp.value().s;
  }

  return ZX_OK;
}

__EXPORT
zx_status_t fs_root_handle(zx_handle_t export_root, zx_handle_t* out_root) {
  // The POSIX flag here requests that the old connection rights be inherited by the new connection.
  // This specifically ensures that WRITABLE connections continue to have the WRITABLE right, while
  // read-only connections do not.
  auto handle_or = fs_management::GetFsRootHandle(
      zx::unowned_channel(export_root),
      fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_POSIX | fio::OPEN_RIGHT_ADMIN);
  if (handle_or.is_error())
    return handle_or.status_value();
  *out_root = handle_or.value().release();
  return ZX_OK;
}
