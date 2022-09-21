// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.fs/cpp/fidl.h>
#include <fidl/fuchsia.driver.compat/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/fidl.h>
#include <fidl/fuchsia.hardware.demo/cpp/fidl.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/driver2/outgoing_directory.h>
#include <zircon/errors.h>

namespace {

// Connect to parent device node using fuchsia.driver.compat.Service
zx::status<fidl::ClientEnd<fuchsia_driver_compat::Device>> ConnectToParentDevice(
    fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir, std::string_view name) {
  auto result = component::OpenServiceAt<fuchsia_driver_compat::Service>(svc_dir, name);
  if (result.is_error()) {
    return result.take_error();
  }
  return result.value().connect_device();
}

// Return the topological path of the parent device node.
zx::status<std::string> GetTopologicalPath(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir) {
  auto parent_client = ConnectToParentDevice(svc_dir, "default");
  if (parent_client.is_error()) {
    return parent_client.take_error();
  }
  fidl::SyncClient parent{std::move(parent_client.value())};

  auto result = parent->GetTopologicalPath();
  if (result.is_error()) {
    const auto& error = result.error_value();
    return zx::error(error.status());
  }

  return zx::ok(result->path());
}

// Connect to the fuchsia.devices.fs.Exporter protocol
zx::status<fidl::ClientEnd<fuchsia_device_fs::Exporter>> ConnectToDeviceExporter(
    fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir) {
  auto exporter = component::ConnectAt<fuchsia_device_fs::Exporter>(svc_dir);
  if (exporter.is_error()) {
    return exporter.take_error();
  }
  return exporter;
}

// Create an exported directory handle using fuchsia.devices.fs.Exporter
zx::status<fidl::ServerEnd<fuchsia_io::Directory>> ExportDevfsEntry(
    fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir, std::string service_path,
    std::string devfs_path, uint32_t protocol_id) {
  // Connect to the devfs exporter service
  auto exporter_client = ConnectToDeviceExporter(svc_dir);
  if (exporter_client.is_error()) {
    return exporter_client.take_error();
  }
  fidl::SyncClient exporter{std::move(exporter_client.value())};

  // Serve a connection for devfs clients
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  // Export the client side of the service connection to devfs
  auto result =
      exporter->Export({std::move(endpoints->client), service_path, devfs_path, protocol_id});
  if (result.is_error()) {
    const auto& error = result.error_value();
    if (error.is_transport_error()) {
      // Error occurred in the FIDL transport
      return zx::error(error.transport_error().status());
    } else {
      // Error response returned by the exporter service
      return zx::error(error.application_error());
    }
  }
  return zx::ok(std::move(endpoints->server));
}

}  // namespace

namespace demo_number {

const std::string kDriverName = "demo_number";

// FIDL server implementation for the `fuchsia.hardware.demo/Demo` protocol.
class DemoNumberServer : public fidl::Server<fuchsia_hardware_demo::Demo> {
 public:
  DemoNumberServer(driver::Logger* logger) : logger_(logger) {}

  void GetNumber(GetNumberRequest& request, GetNumberCompleter::Sync& completer) override {
    completer.Reply(current_number);
    current_number += 1;
  }

  // This method is called when a server connection is torn down.
  void OnUnbound(fidl::UnbindInfo info, fidl::ServerEnd<fuchsia_hardware_demo::Demo> server_end) {
    FDF_LOGL(INFO, (*logger_), "Client connection unbound: %s", info.status_string());
  }

 private:
  uint32_t current_number = 0;
  driver::Logger* logger_;
};

// This class represents the driver instance.
class DemoNumber : public driver::DriverBase {
 public:
  DemoNumber(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : DriverBase(kDriverName, std::move(start_args), std::move(driver_dispatcher)) {}

  // Called by the driver framework to initialize the driver instance.
  zx::status<> Start() override {
    // Add the fuchsia.hardware.demo/Demo protocol to be served as
    // "/svc/fuchsia.hardware.demo/default/demo"
    driver::ServiceInstanceHandler handler;
    fuchsia_hardware_demo::Service::Handler service(&handler);

    auto result =
        service.add_demo([this](fidl::ServerEnd<fuchsia_hardware_demo::Demo> request) -> void {
          // Bind each connection request to a fuchsia.hardware.demo/Demo server instance.
          auto demo_impl = std::make_unique<DemoNumberServer>(&logger_);
          fidl::BindServer(dispatcher(), std::move(request), std::move(demo_impl),
                           std::mem_fn(&DemoNumberServer::OnUnbound));
        });
    ZX_ASSERT(result.is_ok());

    result = context().outgoing()->AddService<fuchsia_hardware_demo::Service>(std::move(handler));
    if (result.is_error()) {
      FDF_SLOG(ERROR, "Failed to add Demo service", KV("status", result.status_string()));
      return result.take_error();
    }

    const std::string service_path = std::string("svc/") + fuchsia_hardware_demo::Service::Name +
                                     "/" + component::kDefaultInstance + "/demo";

    // Construct a devfs path that matches the device nodes topological path
    auto path_result = GetTopologicalPath(context().incoming()->svc_dir());
    if (path_result.is_error()) {
      FDF_SLOG(ERROR, "Failed to get topological path", KV("status", path_result.status_string()));
      return path_result.take_error();
    }
    auto devfs_path = path_result.value().append("/").append(kDriverName);
    FDF_LOG(INFO, "Exporting device to: %s", devfs_path.c_str());

    // Export an entry to devfs for fuchsia.hardware.demo as a generic device
    auto devfs_dir = ExportDevfsEntry(context().incoming()->svc_dir(), service_path, devfs_path, 0);
    if (devfs_dir.is_error()) {
      FDF_SLOG(ERROR, "Failed to export service", KV("status", devfs_dir.status_string()));
      return devfs_dir.take_error();
    }

    // Serve an additional outgoing endpoint for devfs clients
    auto status = context().outgoing()->Serve(std::move(devfs_dir.value()));
    if (status.is_error()) {
      FDF_SLOG(ERROR, "Failed to serve devfs directory", KV("status", status.status_string()));
      return status.take_error();
    }

    return zx::ok();
  }

  // Called by the driver framework before the driver instance is destroyed.
  void Stop() override { FDF_LOG(INFO, "Driver unloaded: %s", kDriverName.c_str()); }
};

}  // namespace demo_number

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<demo_number::DemoNumber>);
