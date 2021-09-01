// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/ldsvc/c/fidl.h>
#include <getopt.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/boot/image.h>
#include <zircon/device/vfs.h>
#include <zircon/dlfcn.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <thread>

#include <fbl/unique_fd.h>
#include <ramdevice-client/ramdisk.h>
#include <zstd/zstd.h>

#include "block-watcher.h"
#include "fs-manager.h"
#include "metrics.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/storage/fshost/deprecated-loader-service.h"

namespace fio = fuchsia_io;

namespace fshost {
namespace {

constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;

zx_status_t DecompressZstd(zx::vmo& input, uint64_t input_offset, size_t input_size,
                           zx::vmo& output, uint64_t output_offset, size_t output_size) {
  auto input_buffer = std::make_unique<uint8_t[]>(input_size);
  zx_status_t status = input.read(input_buffer.get(), input_offset, input_size);
  if (status != ZX_OK) {
    return status;
  }

  auto output_buffer = std::make_unique<uint8_t[]>(output_size);

  auto rc = ZSTD_decompress(output_buffer.get(), output_size, input_buffer.get(), input_size);
  if (ZSTD_isError(rc) || rc != output_size) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  return output.write(output_buffer.get(), output_offset, output_size);
}

// Get ramdisk from the boot items service.
zx_status_t get_ramdisk(zx::vmo* ramdisk_vmo) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect(kItemsPath, remote.release());
  if (status != ZX_OK) {
    return status;
  }
  uint32_t length;
  return fuchsia_boot_ItemsGet(local.get(), ZBI_TYPE_STORAGE_RAMDISK, 0,
                               ramdisk_vmo->reset_and_get_address(), &length);
}

int RamctlWatcher(void* arg) {
  zx_status_t status = wait_for_device("/dev/sys/platform/00:00:2d/ramctl", ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to open /dev/sys/platform/00:00:2d/ramctl: " << strerror(errno);
    return -1;
  }

  zx::vmo ramdisk_vmo(static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(arg)));

  zbi_header_t header;
  status = ramdisk_vmo.read(&header, 0, sizeof(header));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot read ZBI_TYPE_STORAGE_RAMDISK item header: "
                   << zx_status_get_string(status);
    return -1;
  }
  if (!(header.flags & ZBI_FLAG_VERSION) || header.magic != ZBI_ITEM_MAGIC ||
      header.type != ZBI_TYPE_STORAGE_RAMDISK) {
    FX_LOGS(ERROR) << "invalid ZBI_TYPE_STORAGE_RAMDISK item header";
    return -1;
  }

  zx::vmo vmo;
  if (header.flags & ZBI_FLAG_STORAGE_COMPRESSED) {
    status = zx::vmo::create(header.extra, 0, &vmo);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "cannot create VMO for uncompressed RAMDISK: "
                     << zx_status_get_string(status);
      return -1;
    }
    status = DecompressZstd(ramdisk_vmo, sizeof(zbi_header_t), header.length, vmo, 0, header.extra);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to decompress RAMDISK: " << zx_status_get_string(status);
      return -1;
    }
  } else {
    // TODO(fxbug.dev/34597): The old code ignored uncompressed items too, and
    // silently.  Really the protocol should be cleaned up so the VMO arrives
    // without the header in it and then it could just be used here directly
    // if uncompressed (or maybe bootsvc deals with decompression in the first
    // place so the uncompressed VMO is always what we get).
    FX_LOGS(ERROR) << "ignoring uncompressed RAMDISK item in ZBI";
    return -1;
  }

  ramdisk_client* client;
  status = ramdisk_create_from_vmo(vmo.release(), &client);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create ramdisk from ZBI_TYPE_STORAGE_RAMDISK";
  } else {
    FX_LOGS(INFO) << "ZBI_TYPE_STORAGE_RAMDISK attached";
  }
  return 0;
}

// Initialize the fshost namespace.
//
// |fs_root_client| is mapped to "/fs", and represents the filesystem of devmgr.
zx_status_t BindNamespace(zx::channel fs_root_client) {
  fdio_ns_t* ns;
  zx_status_t status;
  if ((status = fdio_ns_get_installed(&ns)) != ZX_OK) {
    FX_LOGS(ERROR) << "cannot get namespace: " << status;
    return status;
  }

  // Bind "/fs".
  if ((status = fdio_ns_bind(ns, "/fs", fs_root_client.release())) != ZX_OK) {
    FX_LOGS(ERROR) << "cannot bind /fs to namespace: " << status;
    return status;
  }

  // Bind "/system".
  {
    zx::channel client, server;
    if ((status = zx::channel::create(0, &client, &server)) != ZX_OK) {
      return status;
    }
    if ((status = fdio_open("/fs/system",
                            ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE | ZX_FS_RIGHT_ADMIN,
                            server.release())) != ZX_OK) {
      FX_LOGS(ERROR) << "cannot open connection to /system: " << status;
      return status;
    }
    if ((status = fdio_ns_bind(ns, "/system", client.release())) != ZX_OK) {
      FX_LOGS(ERROR) << "cannot bind /system to namespace: " << status;
      return status;
    }
  }
  return ZX_OK;
}

Config GetConfig(const FshostBootArgs& boot_args) {
  std::ifstream file("/pkg/config/fshost");
  Config::Options options;
  if (file) {
    options = Config::ReadOptions(file);
  } else {
    options = Config::DefaultOptions();
  }
  if (boot_args.netboot()) {
    options[Config::kNetboot] = std::string();
  }
  if (boot_args.check_filesystems()) {
    options[Config::kCheckFilesystems] = std::string();
  }
  if (boot_args.wait_for_data()) {
    options[Config::kWaitForData] = std::string();
  }
  return Config(std::move(options));
}

std::shared_ptr<loader::LoaderServiceBase> SetUpLoaderService(const async::Loop& loop) {
  // Set up the fshost loader service, which can load libraries from either /system/lib or
  // /boot/lib.
  // TODO(fxbug.dev/34633): This loader is DEPRECATED and should be deleted. Do not add new
  // usages.
  fbl::unique_fd root_fd;
  if (zx_status_t status =
          fdio_open_fd("/",
                       fio::wire::kOpenFlagDirectory | fio::wire::kOpenRightReadable |
                           fio::wire::kOpenRightExecutable,
                       root_fd.reset_and_get_address());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to open namespace root: " << zx_status_get_string(status);
    return nullptr;
  }
  auto loader =
      DeprecatedBootSystemLoaderService::Create(loop.dispatcher(), std::move(root_fd), "fshost");

  // Replace default loader service with a connection to our own.
  // TODO(bryanhenry): This is unnecessary and will be removed in a subsequent change. Left in to
  // minimize behavior differences per change.
  auto conn = loader->Connect();
  if (conn.is_error()) {
    FX_LOGS(ERROR) << "failed to create loader connection: " << conn.status_string();
    return nullptr;
  }
  zx_handle_close(dl_set_loader_service(std::move(conn)->TakeChannel().release()));
  return loader;
}

int Main(bool disable_block_watcher) {
  auto boot_args = FshostBootArgs::Create();
  Config config = GetConfig(*boot_args);

  FX_LOGS(INFO) << "Config: " << config;

  async::Loop loader_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  std::shared_ptr<loader::LoaderServiceBase> loader;
  if (!config.is_set(Config::kUseDefaultLoader)) {
    zx_status_t status = loader_loop.StartThread("fshost-loader");
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to start loader thread: " << zx_status_get_string(status);
      return EXIT_FAILURE;
    }
    loader = SetUpLoaderService(loader_loop);
    if (!loader) {
      return EXIT_FAILURE;
    }
  }

  // Initialize the local filesystem in isolation.
  zx::channel dir_request(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  zx::channel lifecycle_request(zx_take_startup_handle(PA_LIFECYCLE));

  auto metrics = DefaultMetrics();
  metrics->Detach();
  FsManager fs_manager(boot_args, std::move(metrics));

  if (config.netboot()) {
    FX_LOGS(INFO) << "disabling automount";
  }

  BlockWatcher watcher(fs_manager, &config);

  zx::channel driver_admin, remote;
  zx_status_t status = zx::channel::create(0, &driver_admin, &remote);
  if (status) {
    FX_LOGS(ERROR) << "error creating channel: " << zx_status_get_string(status);
    return EXIT_FAILURE;
  }

  status = fdio_service_connect("/svc/fuchsia.device.manager.Administrator", remote.release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "error connecting to device_manager: " << zx_status_get_string(status);
    return EXIT_FAILURE;
  }

  status = fs_manager.Initialize(std::move(dir_request), std::move(lifecycle_request),
                                 std::move(driver_admin), std::move(loader), watcher);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot initialize FsManager: " << zx_status_get_string(status);
    return EXIT_FAILURE;
  }

  // Serve the root filesystems in our own namespace.
  zx::channel fs_root_client, fs_root_server;
  status = zx::channel::create(0, &fs_root_client, &fs_root_server);
  if (status != ZX_OK) {
    return EXIT_FAILURE;
  }
  status = fs_manager.ServeRoot(std::move(fs_root_server));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot serve devmgr's root filesystem";
    return EXIT_FAILURE;
  }

  // Initialize namespace, and begin monitoring for a termination event.
  status = BindNamespace(std::move(fs_root_client));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot bind namespace";
    return EXIT_FAILURE;
  }

  // If there is a ramdisk, setup the ramctl filesystems.
  zx::vmo ramdisk_vmo;
  status = get_ramdisk(&ramdisk_vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to get ramdisk" << zx_status_get_string(status);
  } else if (ramdisk_vmo.is_valid()) {
    thrd_t t;

    int err = thrd_create_with_name(
        &t, &RamctlWatcher, reinterpret_cast<void*>(static_cast<uintptr_t>(ramdisk_vmo.release())),
        "ramctl-filesystems");
    if (err != thrd_success) {
      FX_LOGS(ERROR) << "failed to start ramctl-filesystems: " << err;
    }
    thrd_detach(t);
  }

  if (disable_block_watcher) {
    FX_LOGS(INFO) << "block-watcher disabled";
  } else {
    watcher.Run();
  }

  fs_manager.WaitForShutdown();
  FX_LOGS(INFO) << "terminating";
  return EXIT_SUCCESS;
}

}  // namespace
}  // namespace fshost

int main(int argc, char** argv) {
  int disable_block_watcher = false;
  option options[] = {
      {"disable-block-watcher", no_argument, &disable_block_watcher, true},
  };
  while (getopt_long(argc, argv, "", options, nullptr) != -1) {
  }

  return fshost::Main(disable_block_watcher);
}
