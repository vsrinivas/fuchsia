// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples.gizmo/cpp/driver/wire.h>
#include <fidl/fuchsia.gizmo.protocol/cpp/wire.h>
#include <lib/driver/compat/cpp/compat.h>
#include <lib/driver/compat/cpp/context.h>
#include <lib/driver/component/cpp/driver_cpp.h>
#include <lib/driver/component/cpp/service_client.h>

namespace driver_transport {

// Protocol served to client components over devfs.
class TestProtocolServer : public fidl::WireServer<fuchsia_gizmo_protocol::TestingProtocol> {
 public:
  explicit TestProtocolServer() {}

  void GetValue(GetValueCompleter::Sync& completer) { completer.Reply(0x1234); }
};

class ChildDriverTransportDriver : public driver::DriverBase {
 public:
  ChildDriverTransportDriver(driver::DriverStartArgs start_args,
                             fdf::UnownedDispatcher driver_dispatcher)
      : DriverBase("transport-child", std::move(start_args), std::move(driver_dispatcher)),
        arena_(fdf::Arena('EXAM')) {}

  zx::result<> Start() override {
    // Publish `fuchsia.gizmo.protocol.Service` to the outgoing directory.
    component::ServiceInstanceHandler handler;
    fuchsia_gizmo_protocol::Service::Handler service(&handler);

    auto protocol_handler =
        [this](fidl::ServerEnd<fuchsia_gizmo_protocol::TestingProtocol> request) -> void {
      auto server_impl = std::make_unique<TestProtocolServer>();
      fidl::BindServer(dispatcher(), std::move(request), std::move(server_impl));
    };
    auto result = service.add_testing(protocol_handler);
    ZX_ASSERT(result.is_ok());

    result = context().outgoing()->AddService<fuchsia_gizmo_protocol::Service>(std::move(handler));
    if (result.is_error()) {
      FDF_SLOG(ERROR, "Failed to add service", KV("status", result.status_string()));
      return result.take_error();
    }

    // Connect to the `fuchsia.examples.gizmo.Service` provided by the parent.
    result = ConnectGizmoService();
    if (result.is_error()) {
      FDF_SLOG(ERROR, "Failed to connect client", KV("status", result.status_string()));
      return result.take_error();
    }

    client_.buffer(arena_)->GetHardwareId().ThenExactlyOnce(
        fit::bind_member<&ChildDriverTransportDriver::HardwareIdResult>(this));

    return zx::ok();
  }

  // Connect to the parent's offered service.
  zx::result<> ConnectGizmoService() {
    auto connect_result =
        driver::Connect<fuchsia_examples_gizmo::Service::Device>(*context().incoming());
    if (connect_result.is_error()) {
      FDF_SLOG(ERROR, "Failed to connect gizmo device protocol.",
               KV("status", connect_result.status_string()));
      return connect_result.take_error();
    }
    client_ = fdf::WireClient(std::move(connect_result.value()), driver_dispatcher()->get());

    return zx::ok();
  }

  // Asynchronous GetHardwareId result callback.
  void HardwareIdResult(
      fdf::WireUnownedResult<fuchsia_examples_gizmo::Device::GetHardwareId>& result) {
    if (!result.ok()) {
      FDF_SLOG(ERROR, "Failed to request hardware ID.", KV("status", result.status_string()));
      node().reset();
      return;
    } else if (result->is_error()) {
      FDF_SLOG(ERROR, "Hardware ID request returned an error.",
               KV("status", result->error_value()));
      node().reset();
      return;
    }
    FDF_SLOG(INFO, "Transport client hardware.", KV("response", result.value().value()->response));

    client_.buffer(arena_)->GetFirmwareVersion().ThenExactlyOnce(
        fit::bind_member<&ChildDriverTransportDriver::FirmwareVersionResult>(this));
  }

  // Asynchronous GetFirmwareVersion result callback.
  void FirmwareVersionResult(
      fdf::WireUnownedResult<fuchsia_examples_gizmo::Device::GetFirmwareVersion>& result) {
    if (!result.ok()) {
      FDF_SLOG(ERROR, "Failed to request firmware version.", KV("status", result.status_string()));
      node().reset();
      return;
    } else if (result->is_error()) {
      FDF_SLOG(ERROR, "Firmware version request returned an error.",
               KV("status", result->error_value()));
      node().reset();
      return;
    }
    FDF_SLOG(INFO, "Transport client firmware.", KV("major", result.value().value()->major),
             KV("minor", result.value().value()->minor));

    compat::Context::ConnectAndCreate(
        &context(), dispatcher(),
        fit::bind_member<&ChildDriverTransportDriver::ExportService>(this));
  }

  // Publish offered services for client components.
  void ExportService(zx::result<std::shared_ptr<compat::Context>> result) {
    if (!result.is_ok()) {
      FDF_LOG(ERROR, "Call to Context::ConnectAndCreate failed: %s", result.status_string());
      node().reset();
      return;
    }
    compat_context_ = std::move(*result);
    child_ = compat::DeviceServer(std::string(name()), 0, compat_context_->TopologicalPath(name()));

    // Export `fuchsia.gizmo.protocol.Service` to devfs.
    auto service_path = std::string(fuchsia_gizmo_protocol::Service::Name) + "/" +
                        component::kDefaultInstance + "/" +
                        fuchsia_gizmo_protocol::Service::Testing::Name;
    auto status = compat_context_->devfs_exporter().ExportSync(
        service_path, child_->topological_path(), fuchsia_device_fs::ExportOptions());
    if (status != ZX_OK) {
      FDF_LOG(ERROR, "Failed to export to devfs: %s", zx_status_get_string(status));
      node().reset();
      return;
    }
  }

 private:
  fdf::Arena arena_;
  fdf::WireClient<fuchsia_examples_gizmo::Device> client_;
  std::optional<compat::DeviceServer> child_;
  std::shared_ptr<compat::Context> compat_context_;
};

}  // namespace driver_transport

FUCHSIA_DRIVER_RECORD_CPP_V3(driver::Record<driver_transport::ChildDriverTransportDriver>);
