// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.compat/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/driver2/devfs_exporter.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/driver_compat/compat.h>
#include <lib/driver_compat/symbols.h>
#include <lib/inspect/component/cpp/component.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <zircon/errors.h>

#include "src/ui/input/drivers/hid-input-report/input-report.h"

namespace fdf2 = fuchsia_driver_framework;
namespace fio = fuchsia_io;

namespace {

class InputReportDriver : public driver::DriverBase {
 public:
  InputReportDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher dispatcher)
      : DriverBase("InputReport", std::move(start_args), std::move(dispatcher)) {}

  zx::status<> Start() override {
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

    // Get the topological path and create our child when it completes.
    parent_client_->GetTopologicalPath().Then([this](auto& result) {
      auto status = CreateAndServeDevice(std::move(result->path()));
      if (!status.is_ok()) {
        FDF_LOG(ERROR, "Call to CreateAndServeDevice failed: %s", status.status_string());
        ScheduleStop();
      }
    });
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

  zx::status<> CreateAndServeDevice(std::string topological_path) {
    // Create our child device and FIDL server.
    child_ = compat::DeviceServer("InputReport", ZX_PROTOCOL_INPUTREPORT,
                                  topological_path + "/InputReport", {});
    auto status = context().outgoing()->component().AddProtocol<fuchsia_input_report::InputDevice>(
        &input_report_.value(), "InputReport");
    if (status.is_error()) {
      return status.take_error();
    }
    exporter_.Export(std::string("svc/").append(child_->name()), child_->topological_path(), {},
                     ZX_PROTOCOL_INPUTREPORT, [this](zx_status_t status) {
                       if (status != ZX_OK) {
                         FDF_LOG(WARNING, "Failed to export to devfs: %s",
                                 zx_status_get_string(status));
                         ScheduleStop();
                       }
                     });
    return zx::ok();
  }

  // Calling this function drops our node handle, which tells the DriverFramework to call Stop
  // on the Driver.
  void ScheduleStop() { node().reset(); }

  std::optional<hid_input_report_dev::InputReport> input_report_;
  std::optional<inspect::ComponentInspector> exposed_inspector_;
  std::optional<compat::DeviceServer> child_;
  fidl::Client<fuchsia_driver_compat::Device> parent_client_;
  driver::DevfsExporter exporter_;
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
