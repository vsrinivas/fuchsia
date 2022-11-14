// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/codecs/da7219/da7219-dfv2.h"

#include <fidl/fuchsia.driver.compat/cpp/wire.h>
#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/driver/component/cpp/service_client.h>

namespace audio::da7219 {

zx::result<fidl::ClientEnd<fuchsia_hardware_i2c::Device>> Driver::GetI2cClient() const {
  auto i2c_endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
  if (i2c_endpoints.is_error()) {
    return i2c_endpoints.take_error();
  }
  auto i2c_client = context().incoming()->OpenService<fuchsia_driver_compat::Service>("i2c000");
  if (i2c_client.status_value() != ZX_OK) {
    return i2c_client.take_error();
  }
  auto i2c_fidl =
      fidl::WireCall(*i2c_client.value().connect_device())
          ->ConnectFidl(fidl::StringView::FromExternal(
                            fidl::DiscoverableProtocolName<fuchsia_hardware_i2c::Device>),
                        i2c_endpoints->server.TakeChannel());
  if (!i2c_fidl.ok()) {
    return zx::error(i2c_fidl.status());
  }
  return zx::ok(std::move(i2c_endpoints->client));
}

zx::result<zx::interrupt> Driver::GetIrq() const {
  auto acpi_endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::Device>();
  if (acpi_endpoints.is_error()) {
    return acpi_endpoints.take_error();
  }
  auto acpi_client = context().incoming()->OpenService<fuchsia_driver_compat::Service>("acpi");
  if (!acpi_client.is_ok()) {
    return acpi_client.take_error();
  }
  auto acpi_fidl =
      fidl::WireCall(*acpi_client.value().connect_device())
          ->ConnectFidl(fidl::StringView::FromExternal(
                            fidl::DiscoverableProtocolName<fuchsia_hardware_acpi::Device>),
                        acpi_endpoints->server.TakeChannel());
  if (!acpi_fidl.ok()) {
    return zx::error(acpi_fidl.status());
  }

  auto irq = fidl::WireCall(acpi_endpoints->client)->MapInterrupt(0);
  if (!irq.ok() || irq.value().is_error()) {
    DA7219_LOG(ERROR, "Could not get IRQ: %s",
               irq.ok() ? zx_status_get_string(irq.value().error_value())
                        : irq.FormatDescription().data());
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  return zx::ok(std::move(irq.value().value()->irq));
}

zx::result<> Driver::Start() {
  auto i2c = GetI2cClient();
  if (!i2c.is_ok()) {
    DA7219_LOG(ERROR, "Could not get I2C client");
    return zx::error(i2c.status_value());
  }
  auto irq = GetIrq();
  if (!irq.is_ok()) {
    DA7219_LOG(ERROR, "Could not get IRQ");
    return zx::error(i2c.status_value());
  }

  // There is a core class that implements the core logic and interaction with the hardware, and a
  // Server class that allows the creation of multiple instances (one for input and one for output).
  core_ = std::make_shared<Core>(logger_.get(), std::move(i2c.value()), std::move(irq.value()));
  zx_status_t status = core_->Initialize();
  if (status != ZX_OK) {
    DA7219_LOG(ERROR, "Could not initialize");
    return zx::error(status);
  }

  zx::result<> output = Serve(std::string(name()).append("-output"));
  if (!output.is_ok()) {
    FDF_SLOG(ERROR, "Could not serve output server", KV("status", output.status_string()));
    return zx::error(output.status_value());
  }

  FDF_SLOG(INFO, "Started");

  return zx::ok();
}

zx::result<> Driver::Serve(std::string_view name) {
  // Serve the fuchsia.hardware.audio/CodecConnector protocol to clients through the
  // fuchsia.hardware.audio/CodecConnectorService wrapper.
  component::ServiceInstanceHandler handler;
  fuchsia_hardware_audio::CodecConnectorService::Handler service(&handler);

  auto result = service.add_codec_connector(
      [this](fidl::ServerEnd<fuchsia_hardware_audio::CodecConnector> request) -> void {
        server_output_ = std::make_unique<ServerConnector>(logger_.get(), core_, false);
        fidl::BindServer<fidl::WireServer<fuchsia_hardware_audio::CodecConnector>>(
            dispatcher(), std::move(request), server_output_.get());
      });
  ZX_ASSERT(result.is_ok());

  result = context().outgoing()->AddService<fuchsia_hardware_audio::CodecConnectorService>(
      std::move(handler));
  if (result.is_error()) {
    FDF_SLOG(ERROR, "Failed to add service", KV("status", result.status_string()));
    return result.take_error();
  }

  // Serve using devfs.
  compat::Context::ConnectAndCreate(
      &context(), dispatcher(),
      [this,
       name_copy = std::string(name)](zx::result<std::unique_ptr<compat::Context>> result) mutable {
        if (result.is_error()) {
          FDF_SLOG(ERROR, "Failed to get compat::Context", KV("status", result.status_string()));
          // Reset the node to signal unbind to the driver framework.
          node().reset();
          return;
        }
        compat_context_ = std::move(result.value());
        auto devfs_path = compat_context_->TopologicalPath(name_copy);
        auto service_path = std::string(fuchsia_hardware_audio::CodecConnectorService::Name) + "/" +
                            component::kDefaultInstance + "/" +
                            fuchsia_hardware_audio::CodecConnectorService::CodecConnector::Name;

        auto status = compat_context_->devfs_exporter().ExportSync(
            service_path, devfs_path, fuchsia_device_fs::ExportOptions(), 6 /*ZX_PROTOCOL_CODEC*/);
        if (status != ZX_OK) {
          FDF_SLOG(ERROR, "Failed to export to devfs: %s",
                   KV("status", zx_status_get_string(status)));
          // Reset the node to signal unbind to the driver framework.
          node().reset();
          return;
        }

        FDF_SLOG(INFO, "Exported", KV("service_path", service_path.c_str()),
                 KV("devfs_path", devfs_path.c_str()));
      });
  return zx::ok();
}

}  // namespace audio::da7219

FUCHSIA_DRIVER_RECORD_CPP_V3(driver::Record<audio::da7219::Driver>);
