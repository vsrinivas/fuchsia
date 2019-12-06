// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

namespace fio = ::llcpp::fuchsia::io;
namespace fshost = ::llcpp::fuchsia::fshost;

namespace {
void UnmountHandle(zx_handle_t export_root, bool wait_until_ready) {
  zx::channel root;
  fs_root_handle(export_root, root.reset_and_get_address());
  fs::Vfs::UnmountHandle(std::move(root), wait_until_ready ? zx::time::infinite() : zx::time(0));
}

zx_status_t InitNativeFs(const char* binary, zx::channel device, const init_options_t& options,
                         zx_handle_t* out_export_root) {
  zx_status_t status;
  constexpr size_t kNumHandles = 2;

  zx::channel export_client, export_server;
  if ((status = zx::channel::create(0, &export_client, &export_server)) != ZX_OK) {
    return status;
  }

  std::array<zx_handle_t, kNumHandles> handles = {device.release(), export_server.release()};
  std::array<uint32_t, kNumHandles> ids = {FS_HANDLE_BLOCK_DEVICE_ID, PA_DIRECTORY_REQUEST};

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
  argv.push_back("mount");
  argv.push_back(nullptr);
  int argc = static_cast<int>(argv.size() - 1);

  auto cleanup = fbl::MakeAutoCall([&export_client, options]() {
    UnmountHandle(export_client.get(), options.wait_until_ready);
  });

  if ((status = options.callback(argc, argv.data(), handles.data(), ids.data(), kNumHandles)) !=
      ZX_OK) {
    return status;
  }

  if (options.wait_until_ready) {
    // Wait until the filesystem is ready to take incoming requests
    zx_signals_t observed;
    zx_signals_t signals = ZX_USER_SIGNAL_0  // the filesystem is initialized and serving requests
                           | ZX_CHANNEL_PEER_CLOSED;  // filesystem closed the channel on error
    status = export_client.wait_one(signals, zx::time::infinite(), &observed);
    if (status != ZX_OK) {
      return status;
    }
    if (observed & ZX_CHANNEL_PEER_CLOSED) {
      return ZX_ERR_BAD_STATE;
    }
  }
  cleanup.cancel();

  *out_export_root = export_client.release();
  return ZX_OK;
}

}  // namespace

const init_options_t default_init_options = {
    .readonly = false,
    .verbose_mount = false,
    .collect_metrics = false,
    .wait_until_ready = true,
    .enable_journal = true,
    .enable_pager = false,
    .callback = launch_stdio_async,
};

__EXPORT
zx_status_t fs_init(zx_handle_t device_handle, disk_format_t df, const init_options_t* options,
                    zx_handle_t* out_export_root) {
  zx::channel device(device_handle);

  switch (df) {
    case DISK_FORMAT_MINFS:
      return InitNativeFs("/boot/bin/minfs", std::move(device), *options, out_export_root);
    case DISK_FORMAT_BLOBFS:
      return InitNativeFs("/boot/bin/blobfs", std::move(device), *options, out_export_root);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
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
  zx_status_t status;

  zx::channel root_client, root_server;
  if ((status = zx::channel::create(0, &root_client, &root_server)) != ZX_OK) {
    return status;
  }

  // The POSIX flag here requests that the old connection rights be inherited by the new connection.
  // This specifically ensures that WRITABLE connections continue to have the WRITABLE right, while
  // read-only connections do not.
  uint32_t flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_POSIX | fio::OPEN_RIGHT_ADMIN;
  auto resp = fio::Directory::Call::Open(zx::unowned_channel(export_root), flags, 0,
                                         fidl::StringView("root"), std::move(root_server));
  if (!resp.ok()) {
    return resp.status();
  }

  *out_root = root_client.release();

  return ZX_OK;
}
