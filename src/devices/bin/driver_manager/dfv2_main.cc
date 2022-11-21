// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.driver.index/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <getopt.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/component/incoming/cpp/service_client.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/zx/event.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <threads.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/policy.h>
#include <zircon/types.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

#include <fbl/string_printf.h>

#include "devfs.h"
#include "driver_host_loader_service.h"
#include "fdio.h"
#include "lib/async/cpp/task.h"
#include "main.h"
#include "src/devices/bin/driver_manager/devfs_exporter.h"
#include "src/devices/bin/driver_manager/device_watcher.h"
#include "src/devices/bin/driver_manager/v2/driver_development_service.h"
#include "src/devices/bin/driver_manager/v2/shutdown_manager.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"
#include "system_instance.h"
#include "v2/driver_runner.h"

// Before this is run, the following is run:
// StdoutToDebuglog::Init();
// fx_logger_set_min_severity
// And boot arguments are obtained
int RunDfv2(DriverManagerParams driver_manager_params,
            fidl::WireSyncClient<fuchsia_boot::Arguments> boot_args) {
  zx_status_t status;
  std::string root_driver = "fuchsia-boot:///#meta/platform-bus.cm";
  if (!driver_manager_params.root_driver.empty()) {
    root_driver = driver_manager_params.root_driver;
  }

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  InspectManager inspect_manager(loop.dispatcher());

  zx::result diagnostics_client = inspect_manager.Connect();
  ZX_ASSERT_MSG(diagnostics_client.is_ok(), "%s", diagnostics_client.status_string());

  std::optional<Devnode> root_devnode;
  Devfs devfs(root_devnode, nullptr, std::move(diagnostics_client.value()));

  // Launch devfs_exporter.
  driver_manager::DevfsExporter devfs_exporter(devfs, &root_devnode.value(), loop.dispatcher());
  devfs_exporter.PublishExporter(outgoing);

  // Launch DriverRunner for DFv2 drivers.
  auto realm_result = component::Connect<fuchsia_component::Realm>();
  if (realm_result.is_error()) {
    return realm_result.error_value();
  }
  auto driver_index_result = component::Connect<fuchsia_driver_index::DriverIndex>();
  if (driver_index_result.is_error()) {
    LOGF(ERROR, "Failed to connect to driver_index: %d", driver_index_result.error_value());
    return driver_index_result.error_value();
  }
  fbl::unique_fd lib_fd;
  {
    status = fdio_open_fd("/boot/lib/",
                          static_cast<uint32_t>(fio::wire::OpenFlags::kDirectory |
                                                fio::wire::OpenFlags::kRightReadable |
                                                fio::wire::OpenFlags::kRightExecutable),
                          lib_fd.reset_and_get_address());
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to open /boot/lib/ : %s", zx_status_get_string(status));
      return status;
    }
  }
  // The loader needs its own thread because DriverManager makes synchronous calls to the
  // DriverHosts, which make synchronous calls to load their shared libraries.
  async::Loop loader_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loader_loop.StartThread("loader-loop");

  auto loader_service =
      DriverHostLoaderService::Create(loader_loop.dispatcher(), std::move(lib_fd));
  dfv2::DriverRunner driver_runner(
      std::move(realm_result.value()), std::move(driver_index_result.value()),
      inspect_manager.inspector(), [loader_service]() { return loader_service->Connect(); },
      loop.dispatcher());
  driver_runner.PublishComponentRunner(outgoing);

  // Find and load v2 Drivers.
  LOGF(INFO, "Starting DriverRunner with root driver URL: %s", root_driver.c_str());
  auto start = driver_runner.StartRootDriver(root_driver);
  if (start.is_error()) {
    return start.error_value();
  }

  driver_manager::DriverDevelopmentService driver_development_service(driver_runner,
                                                                      loop.dispatcher());
  driver_development_service.Publish(outgoing);
  driver_runner.PublishNodeGroupManager(outgoing);
  driver_runner.ScheduleBaseDriversBinding();

  dfv2::ShutdownManager shutdown_manager(&driver_runner, loop.dispatcher());
  async::Loop usb_watcher_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  usb_watcher_loop.StartThread();

  // TODO(https://fxbug.dev/99076) Remove this when this issue is fixed.
  LOGF(INFO, "driver_manager loader loop started");

  fs::SynchronousVfs vfs(loop.dispatcher());

  {
    zx::result devfs_client = devfs.Connect(vfs);
    ZX_ASSERT_MSG(devfs_client.is_ok(), "%s", devfs_client.status_string());
    shutdown_manager.Publish(outgoing, std::move(devfs_client.value()));
  }

  // Serve the USB device watcher protocol.
  {
    zx::result devfs_client = devfs.Connect(vfs);
    ZX_ASSERT_MSG(devfs_client.is_ok(), "%s", devfs_client.status_string());

    const zx::result result = outgoing.AddProtocol<fuchsia_device_manager::DeviceWatcher>(
        [devfs_client = std::move(devfs_client.value()),
         dispatcher = usb_watcher_loop.dispatcher()](
            fidl::ServerEnd<fuchsia_device_manager::DeviceWatcher> request) {
          // Move off the main loop, which is also serving devfs.
          async::PostTask(
              dispatcher, [&devfs_client, dispatcher, request = std::move(request)]() mutable {
                zx::result fd = [&devfs_client]() -> zx::result<fbl::unique_fd> {
                  zx::result endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
                  if (endpoints.is_error()) {
                    return endpoints.take_error();
                  }
                  auto& [client, server] = endpoints.value();

                  if (const zx_status_t status = fdio_open_at(
                          devfs_client.channel().get(), "class/usb-device",
                          static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable |
                                                fuchsia_io::wire::OpenFlags::kRightWritable),
                          server.TakeChannel().release());
                      status != ZX_OK) {
                    return zx::error(status);
                  }

                  fbl::unique_fd fd;
                  if (const zx_status_t status = fdio_fd_create(client.TakeChannel().release(),
                                                                fd.reset_and_get_address());
                      status != ZX_OK) {
                    return zx::error(status);
                  }
                  return zx::ok(std::move(fd));
                }();
                if (fd.is_error()) {
                  request.Close(fd.status_value());
                }
                std::unique_ptr watcher =
                    std::make_unique<DeviceWatcher>(dispatcher, std::move(fd.value()));
                fidl::BindServer(dispatcher, std::move(request), std::move(watcher));
              });
        },
        "fuchsia.hardware.usb.DeviceWatcher");
    ZX_ASSERT_MSG(result.is_ok(), "%s", result.status_string());
  }

  // Add the devfs folder to the tree:
  {
    zx::result devfs_client = devfs.Connect(vfs);
    ZX_ASSERT_MSG(devfs_client.is_ok(), "%s", devfs_client.status_string());
    const zx::result result = outgoing.AddDirectory(std::move(devfs_client.value()), "dev");
    ZX_ASSERT(result.is_ok());
  }

  // Add the diagnostics folder to the tree:
  {
    zx::result diagnostics_client = inspect_manager.Connect();
    ZX_ASSERT_MSG(diagnostics_client.is_ok(), "%s", diagnostics_client.status_string());
    const zx::result result =
        outgoing.AddDirectory(std::move(diagnostics_client.value()), "diagnostics");
    ZX_ASSERT(result.is_ok());
  }

  {
    const zx::result result = outgoing.ServeFromStartupInfo();
    ZX_ASSERT_MSG(result.is_ok(), "%s", result.status_string());
  }

  // TODO(https://fxbug.dev/99076) Remove this when this issue is fixed.
  LOGF(INFO, "driver_manager loader loop started");

  async::PostTask(loop.dispatcher(), [] { LOGF(INFO, "driver_manager main loop is running"); });

  status = loop.Run();
  LOGF(ERROR, "Driver Manager exited unexpectedly: %s", zx_status_get_string(status));
  return status;
}
