// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.devfs.test/cpp/wire.h>
#include <fidl/fuchsia.device.fs/cpp/fidl.h>
#include <fidl/fuchsia.driver.compat/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/driver2/driver2_cpp.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace ft = fuchsia_devfs_test;

namespace {

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

class LifecycleDriver : public driver::DriverBase, public fidl::WireServer<ft::Device> {
 public:
  LifecycleDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : DriverBase("lifeycle-driver", std::move(start_args), std::move(driver_dispatcher)) {}

  zx::status<> Start() override {
    // Serve our Service.
    driver::ServiceInstanceHandler handler;
    ft::Service::Handler service(&handler);

    auto result = service.add_device([this](fidl::ServerEnd<ft::Device> request) -> void {
      fidl::BindServer(dispatcher(), std::move(request), this);
    });
    ZX_ASSERT(result.is_ok());

    result = context().outgoing()->AddService<ft::Service>(std::move(handler));
    if (result.is_error()) {
      FDF_SLOG(ERROR, "Failed to add Demo service", KV("status", result.status_string()));
      return result.take_error();
    }

    auto compat = component::ConnectAt<fuchsia_driver_compat::Service::Device>(
        context().incoming()->svc_dir());
    if (compat.is_error()) {
      FDF_LOG(ERROR, "Error connecting to compat: %s", compat.status_string());
      return compat.take_error();
    }
    auto compat_client = fidl::SyncClient<fuchsia_driver_compat::Device>(std::move(compat.value()));
    std::string export_path = "";
    {
      auto result = compat_client->GetTopologicalPath();
      if (!result.is_ok()) {
        FDF_LOG(ERROR, "Error sending to compat: %s", result.error_value().status_string());
        return zx::error(ZX_ERR_INTERNAL);
      }
      export_path = std::move(result->path());
    }
    export_path.append("/lifecycle-device");

    const auto kServicePath =
        std::string("svc/") + ft::Service::Name + "/" + component::kDefaultInstance + "/device";

    // Export an entry to devfs for fuchsia.hardware.demo as a generic device
    auto devfs_dir =
        ExportDevfsEntry(context().incoming()->svc_dir(), kServicePath, export_path, 0);
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

  // fidl::WireServer<ft::Device>
  void Ping(PingCompleter::Sync& completer) override { completer.Reply(); }
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<LifecycleDriver>);
