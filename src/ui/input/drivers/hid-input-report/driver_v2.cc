// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.compat/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/ddk/debug.h>
#include <lib/driver2/devfs_exporter.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/fpromise/scope.h>
#include <lib/inspect/component/cpp/component.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <zircon/errors.h>

#include "src/devices/lib/compat/compat.h"
#include "src/devices/lib/compat/symbols.h"
#include "src/ui/input/drivers/hid-input-report/input-report.h"

namespace fdf2 = fuchsia_driver_framework;
namespace fio = fuchsia_io;

namespace {

class InputReportDriver : public driver::DriverBase {
 public:
  InputReportDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher dispatcher)
      : DriverBase("InputReport", std::move(start_args), std::move(dispatcher)) {}

  zx::status<> Start() override {
    executor_.emplace(async_dispatcher());
    auto parent_symbol = driver::GetSymbol<compat::device_t*>(symbols(), compat::kDeviceSymbol);

    hid_device_protocol_t proto = {};
    if (parent_symbol->proto_ops.id != ZX_PROTOCOL_HID_DEVICE) {
      FDF_LOG(ERROR, "Didn't find HID_DEVICE protocol");
      return zx::error(ZX_ERR_NOT_FOUND);
    }
    proto.ctx = parent_symbol->context;
    proto.ops = reinterpret_cast<const hid_device_protocol_ops_t*>(parent_symbol->proto_ops.ops);

    ddk::HidDeviceProtocolClient hiddev(&proto);
    if (!hiddev.is_valid()) {
      FDF_LOG(ERROR, "Failed to create hiddev");
      return zx::error(ZX_ERR_INTERNAL);
    }
    input_report_.emplace(std::move(hiddev));

    // Expose the driver's inspect data.
    exposed_inspector_.emplace(inspect::ComponentInspector(
        context().outgoing()->component(), async_dispatcher(), input_report_->Inspector()));

    // Start the inner DFv1 driver.
    input_report_->Start();

    // Connect to DevfsExporter.
    auto status = ConnectToDevfsExporter();
    if (status.is_error()) {
      return status.take_error();
    }

    // Connect to our parent.
    auto result = component::ConnectAt<fuchsia_driver_compat::Service::Device>(
        context().incoming()->svc_dir());
    if (result.is_error()) {
      return result.take_error();
    }
    parent_client_.Bind(std::move(result.value()), async_dispatcher());

    auto compat_connect =
        fpromise::make_result_promise<void, zx_status_t>(fpromise::ok())
            .and_then([this]() {
              fpromise::bridge<void, zx_status_t> topo_bridge;
              parent_client_->GetTopologicalPath().Then(
                  [this, completer = std::move(topo_bridge.completer)](
                      fidl::Result<fuchsia_driver_compat::Device::GetTopologicalPath>&
                          result) mutable {
                    if (result.is_error()) {
                      completer.complete_error(result.error_value().status());
                      return;
                    }
                    parent_topo_path_ = result->path();
                    completer.complete_ok();
                  });
              return topo_bridge.consumer.promise_or(fpromise::error(ZX_ERR_CANCELED));
            })
            // Create our child device and FIDL server.
            .and_then([this]() -> fpromise::promise<void, zx_status_t> {
              child_ = compat::DeviceServer("InputReport", ZX_PROTOCOL_INPUTREPORT,
                                            parent_topo_path_ + "/InputReport", {});
              auto status =
                  context().outgoing()->component().AddProtocol<fuchsia_input_report::InputDevice>(
                      &input_report_.value(), "InputReport");
              if (status.is_error()) {
                return fpromise::make_result_promise<void, zx_status_t>(
                    fpromise::error(status.error_value()));
              }
              auto export_status =
                  exporter_.ExportSync(std::string("svc/").append(child_->name()),
                                       child_->topological_path(), {}, ZX_PROTOCOL_INPUTREPORT);
              if (export_status != ZX_OK) {
                FDF_LOG(WARNING, "Failed to export to devfs: %s",
                        zx_status_get_string(export_status));
              }
              return fpromise::make_result_promise<void, zx_status_t>(fpromise::ok());
            })
            // Error handling.
            .or_else([this](zx_status_t& result) {
              FDF_LOG(WARNING, "Device setup failed with: %s", zx_status_get_string(result));
            });
    executor_->schedule_task(std::move(compat_connect));

    return zx::ok();
  }

 private:
  zx::status<> ConnectToDevfsExporter() {
    // Connect to DevfsExporter.
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    // Serve a connection to outgoing.
    auto status = context().outgoing()->Serve(std::move(endpoints->server));
    if (status.is_error()) {
      return status.take_error();
    }

    auto exporter = driver::DevfsExporter::Create(
        *context().incoming(), async_dispatcher(),
        fidl::WireSharedClient(std::move(endpoints->client), async_dispatcher()));
    if (exporter.is_error()) {
      return zx::error(exporter.error_value());
    }
    exporter_ = std::move(*exporter);
    return zx::ok();
  }

  std::optional<hid_input_report_dev::InputReport> input_report_;
  std::optional<async::Executor> executor_;

  std::optional<inspect::ComponentInspector> exposed_inspector_;

  std::optional<compat::DeviceServer> child_;
  std::string parent_topo_path_;
  fidl::Client<fuchsia_driver_compat::Device> parent_client_;
  driver::DevfsExporter exporter_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

// TODO(fxbug.dev/94884): Figure out how to get logging working.
zx_driver_rec_t __zircon_driver_rec__ = {};

void driver_logf_internal(const zx_driver_t* drv, fx_log_severity_t severity, const char* tag,
                          const char* file, int line, const char* msg, ...) {}

bool driver_log_severity_enabled_internal(const zx_driver_t* drv, fx_log_severity_t severity) {
  return true;
}

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<InputReportDriver>);
