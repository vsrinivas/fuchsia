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
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/sys/component/cpp/service_client.h>
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

#include "component_lifecycle.h"
#include "coordinator.h"
#include "devfs.h"
#include "driver_host_loader_service.h"
#include "fdio.h"
#include "lib/async/cpp/task.h"
#include "src/devices/bin/driver_manager/devfs_exporter.h"
#include "src/devices/bin/driver_manager/device_watcher.h"
#include "src/devices/bin/driver_manager/v2/driver_development_service.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"
#include "system_instance.h"
#include "v2/driver_runner.h"

namespace {

// These are helpers for getting sets of parameters over FIDL
struct DriverManagerParams {
  bool require_system;
  bool suspend_timeout_fallback;
  bool verbose;
  DriverHostCrashPolicy crash_policy;
  std::string root_driver;
  bool use_dfv2;
};

DriverManagerParams GetDriverManagerParams(fidl::WireSyncClient<fuchsia_boot::Arguments>& client) {
  fuchsia_boot::wire::BoolPair bool_req[]{
      {"devmgr.require-system", false},
      {"devmgr.suspend-timeout-fallback", true},
      {"devmgr.verbose", false},
      {"driver_manager.use_driver_framework_v2", false},
  };
  auto bool_resp =
      client->GetBools(fidl::VectorView<fuchsia_boot::wire::BoolPair>::FromExternal(bool_req));
  if (!bool_resp.ok()) {
    return {};
  }

  auto crash_policy = DriverHostCrashPolicy::kRestartDriverHost;
  auto response = client->GetString("driver-manager.driver-host-crash-policy");
  if (response.ok() && !response.value().value.is_null() && !response.value().value.empty()) {
    std::string const crash_policy_str(response.value().value.get());
    if (crash_policy_str == "reboot-system") {
      crash_policy = DriverHostCrashPolicy::kRebootSystem;
    } else if (crash_policy_str == "restart-driver-host") {
      crash_policy = DriverHostCrashPolicy::kRestartDriverHost;
    } else if (crash_policy_str == "do-nothing") {
      crash_policy = DriverHostCrashPolicy::kDoNothing;
    } else {
      LOGF(ERROR, "Unexpected option for driver-manager.driver-host-crash-policy: %s",
           crash_policy_str.c_str());
    }
  }

  std::string root_driver;
  {
    auto response = client->GetString("driver_manager.root-driver");
    if (response.ok() && !response.value().value.is_null() && !response.value().value.empty()) {
      root_driver = std::string(response.value().value.data(), response.value().value.size());
    }
  }

  return {
      .require_system = bool_resp.value().values[0],
      .suspend_timeout_fallback = bool_resp.value().values[1],
      .verbose = bool_resp.value().values[2],
      .crash_policy = crash_policy,
      .root_driver = std::move(root_driver),
      .use_dfv2 = bool_resp.value().values[3],
  };
}

// Get the root job from the root job service.
zx::status<zx::job> get_root_job() {
  zx::status client_end = component::Connect<fuchsia_kernel::RootJob>();
  if (client_end.is_error()) {
    return client_end.take_error();
  }
  fidl::WireResult result = fidl::WireCall(client_end.value())->Get();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  return zx::ok(std::move(result.value().job));
}

// Get the root resource from the root resource service. Not receiving the
// startup handle is logged, but not fatal.  In test environments, it would not
// be present.
zx::status<zx::resource> get_root_resource() {
  zx::status client_end = component::Connect<fuchsia_boot::RootResource>();
  if (client_end.is_error()) {
    return client_end.take_error();
  }
  fidl::WireResult result = fidl::WireCall(client_end.value())->Get();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  return zx::ok(std::move(result.value().resource));
}

// Get the mexec resource from the mexec resource service. Not receiving the
// startup handle is logged, but not fatal.  In test environments, it would not
// be present.
zx::status<zx::resource> get_mexec_resource() {
  zx::status client_end = component::Connect<fuchsia_kernel::MexecResource>();
  if (client_end.is_error()) {
    return client_end.take_error();
  }
  fidl::WireResult result = fidl::WireCall(client_end.value())->Get();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  return zx::ok(std::move(result.value().resource));
}

}  // namespace

int main(int argc, char** argv) {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    LOGF(INFO, "Failed to redirect stdout to debuglog, assuming test environment and continuing");
  }

  auto args_result = component::Connect<fuchsia_boot::Arguments>();
  if (args_result.is_error()) {
    LOGF(ERROR, "Failed to get boot arguments service handle: %s", args_result.status_string());
    return args_result.error_value();
  }

  auto boot_args = fidl::WireSyncClient<fuchsia_boot::Arguments>{std::move(*args_result)};
  auto driver_manager_params = GetDriverManagerParams(boot_args);

  if (driver_manager_params.verbose) {
    fx_logger_t* logger = fx_log_get_logger();
    if (logger) {
      fx_logger_set_min_severity(logger, std::numeric_limits<fx_log_severity_t>::min());
    }
  }
  std::string root_driver = "fuchsia-boot:///#driver/platform-bus.so";
  if (driver_manager_params.use_dfv2) {
    root_driver = "fuchsia-boot:///#meta/platform-bus.cm";
  }
  if (!driver_manager_params.root_driver.empty()) {
    root_driver = driver_manager_params.root_driver;
  }

  SuspendCallback suspend_callback = [](zx_status_t status) {
    if (status != ZX_OK) {
      // TODO(https://fxbug.dev/56208): Change this log back to error once isolated devmgr is fixed.
      LOGF(WARNING, "Error suspending devices while stopping the component:%s",
           zx_status_get_string(status));
    }
    LOGF(INFO, "Exiting driver manager gracefully");
    // TODO(fxb:52627) This event handler should teardown devices and driver hosts
    // properly for system state transitions where driver manager needs to go down.
    // Exiting like so, will not run all the destructors and clean things up properly.
    // Instead the main devcoordinator loop should be quit.
    exit(0);
  };

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  InspectManager inspect_manager(loop.dispatcher());

  CoordinatorConfig config;
  SystemInstance system_instance;
  config.boot_args = &boot_args;
  config.require_system = driver_manager_params.require_system;
  config.verbose = driver_manager_params.verbose;
  config.fs_provider = &system_instance;
  config.path_prefix = "/boot/";
  config.crash_policy = driver_manager_params.crash_policy;

  // Waiting an infinite amount of time before falling back is effectively not
  // falling back at all.
  if (!driver_manager_params.suspend_timeout_fallback) {
    config.suspend_timeout = zx::duration::infinite();
  }

  auto driver_index_client = component::Connect<fuchsia_driver_index::DriverIndex>();
  if (driver_index_client.is_error()) {
    LOGF(ERROR, "Failed to connect to driver_index: %d", driver_index_client.error_value());
    return driver_index_client.error_value();
  }
  config.driver_index = fidl::WireSharedClient<fuchsia_driver_index::DriverIndex>(
      std::move(driver_index_client.value()), loop.dispatcher());

  // TODO(fxbug.dev/33958): Remove all uses of the root resource.
  if (zx::status root_resource = get_root_resource(); root_resource.is_error()) {
    LOGF(INFO, "Failed to get root resource, assuming test environment and continuing (%s)",
         root_resource.status_string());
  } else {
    config.root_resource = std::move(root_resource.value());
  }
  // TODO(fxbug.dev/33957): Remove all uses of the root job.
  zx::status root_job = get_root_job();
  if (root_job.is_error()) {
    LOGF(ERROR, "Failed to get root job: %s", root_job.status_string());
    return root_job.status_value();
  }
  if (zx::status mexec_resource = get_mexec_resource(); mexec_resource.is_error()) {
    LOGF(INFO, "Failed to get mexec resource, assuming test environment and continuing (%s)",
         mexec_resource.status_string());
  } else {
    config.mexec_resource = std::move(mexec_resource.value());
  }

  zx_handle_t oom_event;
  status = zx_system_get_event(root_job.value().get(), ZX_SYSTEM_EVENT_OUT_OF_MEMORY, &oom_event);
  if (status != ZX_OK) {
    LOGF(INFO, "Failed to get OOM event, assuming test environment and continuing");
  } else {
    config.oom_event = zx::event(oom_event);
  }

  async::Loop firmware_loop(&kAsyncLoopConfigNeverAttachToThread);
  firmware_loop.StartThread("firmware-loop");

  Coordinator coordinator(std::move(config), &inspect_manager, loop.dispatcher(),
                          firmware_loop.dispatcher());

  // Services offered to the rest of the system.
  coordinator.InitOutgoingServices(outgoing);

  std::optional<driver_manager::DriverDevelopmentService> driver_development_service;

  // Launch devfs_exporter.
  std::optional<Devnode>& root_node = coordinator.root_device()->self;
  ZX_ASSERT(root_node.has_value());
  driver_manager::DevfsExporter devfs_exporter(coordinator.devfs(), &root_node.value(),
                                               loop.dispatcher());
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
  auto driver_runner =
      dfv2::DriverRunner(std::move(realm_result.value()), std::move(driver_index_result.value()),
                         inspect_manager.inspector(), loop.dispatcher());
  driver_runner.PublishComponentRunner(outgoing);

  // Find and load v1 or v2 Drivers.
  if (!driver_manager_params.use_dfv2) {
    coordinator.set_driver_runner(&driver_runner);
    coordinator.PublishDriverDevelopmentService(outgoing);

    // V1 Drivers.
    status = system_instance.CreateDriverHostJob(root_job.value(), &config.driver_host_job);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to create driver_host job: %s", zx_status_get_string(status));
      return status;
    }

    coordinator.LoadV1Drivers(root_driver);
  } else {
    // V2 Drivers.
    LOGF(INFO, "Starting DriverRunner with root driver URL: %s", root_driver.c_str());
    auto start = driver_runner.StartRootDriver(root_driver);
    if (start.is_error()) {
      return start.error_value();
    }
    driver_development_service.emplace(driver_runner, loop.dispatcher());
    driver_development_service->Publish(outgoing);
    driver_runner.PublishDeviceGroupManager(outgoing);
    driver_runner.ScheduleBaseDriversBinding();
  }

  // Check if whatever launched devmgr gave a channel for component lifecycle events
  fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> component_lifecycle_request(
      zx::channel(zx_take_startup_handle(PA_LIFECYCLE)));
  if (component_lifecycle_request.is_valid()) {
    status = devmgr::ComponentLifecycleServer::Create(loop.dispatcher(), &coordinator,
                                                      std::move(component_lifecycle_request),
                                                      std::move(suspend_callback));
    if (status != ZX_OK) {
      LOGF(ERROR, "driver_manager: Cannot create componentlifecycleserver: %s",
           zx_status_get_string(status));
      return status;
    }
  } else {
    LOGF(INFO,
         "No valid handle found for lifecycle events, assuming test environment and "
         "continuing");
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
  auto loader_service =
      DriverHostLoaderService::Create(loader_loop.dispatcher(), std::move(lib_fd));
  coordinator.set_loader_service_connector([ls = std::move(loader_service)](zx::channel* c) {
    auto conn = ls->Connect();
    if (conn.is_error()) {
      LOGF(ERROR, "Failed to add driver_host loader connection: %s", conn.status_string());
    } else {
      *c = conn->TakeChannel();
    }
    return conn.status_value();
  });
  loader_loop.StartThread();

  // TODO(https://fxbug.dev/99076) Remove this when this issue is fixed.
  LOGF(INFO, "driver_manager loader loop started");

  fs::SynchronousVfs vfs(loop.dispatcher());

  {
    zx::status devfs_client = coordinator.devfs().Connect(vfs);
    ZX_ASSERT_MSG(devfs_client.is_ok(), "%s", devfs_client.status_string());
    system_instance.ServiceStarter(&coordinator, std::move(devfs_client.value()));
  }

  // Serve the USB device watcher protocol.
  {
    zx::status devfs_client = coordinator.devfs().Connect(vfs);
    ZX_ASSERT_MSG(devfs_client.is_ok(), "%s", devfs_client.status_string());

    const zx::status result = outgoing.AddProtocol<fuchsia_device_manager::DeviceWatcher>(
        [devfs_client = std::move(devfs_client.value()), dispatcher = loader_loop.dispatcher()](
            fidl::ServerEnd<fuchsia_device_manager::DeviceWatcher> request) {
          // Move off the main loop, which is also serving devfs.
          async::PostTask(
              dispatcher, [&devfs_client, dispatcher, request = std::move(request)]() mutable {
                zx::status fd = [&devfs_client]() -> zx::status<fbl::unique_fd> {
                  zx::status endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
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

  zx::status diagnostics_client = coordinator.inspect_manager().Connect();
  ZX_ASSERT_MSG(diagnostics_client.is_ok(), "%s", diagnostics_client.status_string());

  zx::status devfs_client = coordinator.devfs().Connect(vfs);
  ZX_ASSERT_MSG(devfs_client.is_ok(), "%s", devfs_client.status_string());

  {
    const zx::status result = outgoing.AddDirectory(std::move(devfs_client.value()), "dev");
    ZX_ASSERT_MSG(result.is_ok(), "%s", result.status_string());
  }
  {
    const zx::status result =
        outgoing.AddDirectory(std::move(diagnostics_client.value()), "diagnostics");
    ZX_ASSERT_MSG(result.is_ok(), "%s", result.status_string());
  }

  {
    const zx::status result = outgoing.ServeFromStartupInfo();
    ZX_ASSERT_MSG(result.is_ok(), "%s", result.status_string());
  }

  async::PostTask(loop.dispatcher(), [] { LOGF(INFO, "driver_manager main loop is running"); });

  coordinator.set_running(true);
  status = loop.Run();
  LOGF(ERROR, "Coordinator exited unexpectedly: %s", zx_status_get_string(status));
  return status;
}
