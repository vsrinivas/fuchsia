// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/fshost/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
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
#include <lib/syslog/global.h>
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

#include <cobalt-client/cpp/collector.h>
#include <fbl/unique_fd.h>
#include <fs/metrics/cobalt_metrics.h>
#include <fs/remote_dir.h>
#include <fs/service.h>
#include <ramdevice-client/ramdisk.h>
#include <zbi-bootfs/zbi-bootfs.h>

#include "block-watcher.h"
#include "fs-manager.h"
#include "metrics.h"
#include "src/storage/fshost/deprecated-loader-service.h"

namespace fio = ::llcpp::fuchsia::io;

namespace devmgr {
namespace {

std::unique_ptr<FsHostMetrics> MakeMetrics() {
  return std::make_unique<FsHostMetrics>(
      std::make_unique<cobalt_client::Collector>(fs_metrics::kCobaltProjectId));
}

constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;

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

zx_status_t MiscDeviceAdded(int dirfd, int event, const char* fn, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE || strcmp(fn, "ramctl") != 0) {
    return ZX_OK;
  }

  zx::vmo ramdisk_vmo(static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(cookie)));

  zbi_header_t header;
  zx_status_t status = ramdisk_vmo.read(&header, 0, sizeof(header));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot read ZBI_TYPE_STORAGE_RAMDISK item header: "
                   << zx_status_get_string(status);
    return ZX_ERR_STOP;
  }
  if (!(header.flags & ZBI_FLAG_VERSION) || header.magic != ZBI_ITEM_MAGIC ||
      header.type != ZBI_TYPE_STORAGE_RAMDISK) {
    FX_LOGS(ERROR) << "invalid ZBI_TYPE_STORAGE_RAMDISK item header";
    return ZX_ERR_STOP;
  }

  zx::vmo vmo;
  if (header.flags & ZBI_FLAG_STORAGE_COMPRESSED) {
    status = zx::vmo::create(header.extra, 0, &vmo);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "cannot create VMO for uncompressed RAMDISK: "
                     << zx_status_get_string(status);
      return ZX_ERR_STOP;
    }
    status = zbi_bootfs::Decompress(ramdisk_vmo, sizeof(zbi_header_t), header.length, vmo, 0,
                                    header.extra);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to decompress RAMDISK: " << zx_status_get_string(status);
      return ZX_ERR_STOP;
    }
  } else {
    // TODO(fxbug.dev/34597): The old code ignored uncompressed items too, and
    // silently.  Really the protocol should be cleaned up so the VMO arrives
    // without the header in it and then it could just be used here directly
    // if uncompressed (or maybe bootsvc deals with decompression in the first
    // place so the uncompressed VMO is always what we get).
    FX_LOGS(ERROR) << "ignoring uncompressed RAMDISK item in ZBI";
    return ZX_ERR_STOP;
  }

  ramdisk_client* client;
  status = ramdisk_create_from_vmo(vmo.release(), &client);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create ramdisk from ZBI_TYPE_STORAGE_RAMDISK";
  } else {
    FX_LOGS(INFO) << "ZBI_TYPE_STORAGE_RAMDISK attached";
  }
  return ZX_ERR_STOP;
}

int RamctlWatcher(void* arg) {
  fbl::unique_fd dirfd(open("/dev/misc", O_DIRECTORY | O_RDONLY));
  if (!dirfd) {
    FX_LOGS(ERROR) << "failed to open /dev/misc: " << strerror(errno);
    return -1;
  }
  fdio_watch_directory(dirfd.get(), &MiscDeviceAdded, ZX_TIME_INFINITE, arg);
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

zx_status_t LogToDebugLog(llcpp::fuchsia::boot::WriteOnlyLog::SyncClient log_client) {
  auto result = log_client.Get();
  if (result.status() != ZX_OK) {
    return result.status();
  }
  char process_name[ZX_MAX_NAME_LEN] = {};
  zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  const char* tag = process_name;
  fx_logger_config_t logger_config{
      .min_severity = fx_logger_get_min_severity(fx_log_get_logger()),
      .console_fd = -1,
      .log_service_channel = ZX_HANDLE_INVALID,
      .tags = &tag,
      .num_tags = 1,
  };
  zx_status_t status = fdio_fd_create(result.Unwrap()->log.release(), &logger_config.console_fd);
  if (status != ZX_OK) {
    return status;
  }
  return fx_log_reconfigure(&logger_config);
}

zx::status<llcpp::fuchsia::boot::WriteOnlyLog::SyncClient> ConnectToWriteLog() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  status = fdio_service_connect("/svc/fuchsia.boot.WriteOnlyLog", remote.release());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(llcpp::fuchsia::boot::WriteOnlyLog::SyncClient(std::move(local)));
}

// Opens handle to log write service and reconfigures syslog to use that
// handle for logging. This is a short term fix for a bug where in on a
// board with userdebug build, no logs show up on serial.
// TODO(fxbug.dev/66476)
void UseDebugLog() {
  auto log_client_or = ConnectToWriteLog();
  if (log_client_or.is_error()) {
    std::cerr << "fshost: failed to get write log: "
              << zx_status_get_string(log_client_or.error_value()) << std::endl;
    return;
  }

  if (auto status = LogToDebugLog(std::move(log_client_or.value())); status != ZX_OK) {
    std::cerr << "fshost: Failed to reconfigure logger to use debuglog: "
              << zx_status_get_string(status) << std::endl;
    return;
  }
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
  if (zx_status_t status = fdio_open_fd(
          "/", fio::OPEN_FLAG_DIRECTORY | fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
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
  zx_handle_close(dl_set_loader_service(std::move(conn).value().release()));
  return loader;
}

int Main(bool disable_block_watcher) {
  auto boot_args = FshostBootArgs::Create();
  Config config = GetConfig(*boot_args);

  if (!config.is_set(Config::kUseSyslog))
    UseDebugLog();

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
  FsManager fs_manager(boot_args, MakeMetrics());

  if (config.netboot()) {
    FX_LOGS(INFO) << "disabling automount";
  }

  BlockWatcher watcher(fs_manager, &config);

  zx_status_t status = fs_manager.Initialize(std::move(dir_request), std::move(lifecycle_request),
                                             std::move(loader), watcher);
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
}  // namespace devmgr

int main(int argc, char** argv) {
  int disable_block_watcher = false;
  option options[] = {
      {"disable-block-watcher", no_argument, &disable_block_watcher, true},
  };
  while (getopt_long(argc, argv, "", options, nullptr) != -1) {
  }

  return devmgr::Main(disable_block_watcher);
}
