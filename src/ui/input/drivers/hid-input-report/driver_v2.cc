// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.compat/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/driver2/devfs_exporter.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/driver_compat/compat.h>
#include <lib/driver_compat/context.h>
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

    // Create our compat context, and serve our device when it's created.
    compat::Context::ConnectAndCreate(&context(), async_dispatcher(), [this](auto result) {
      if (!result.is_ok()) {
        FDF_LOG(ERROR, "Call to Context::ConnectAndCreate failed: %s", result.status_string());
        ScheduleStop();
        return;
      }
      compat_context_ = std::move(*result);
      auto status = CreateAndServeDevice();
      if (!status.is_ok()) {
        FDF_LOG(ERROR, "Call to CreateAndServeDevice failed: %s", status.status_string());
        ScheduleStop();
        return;
      }
    });
    return zx::ok();
  }

 private:
  zx::status<> CreateAndServeDevice() {
    // Create our child device and FIDL server.
    child_ = compat::DeviceServer("InputReport", ZX_PROTOCOL_INPUTREPORT,
                                  compat_context_->TopologicalPath("InputReport"), {});
    auto status = context().outgoing()->component().AddProtocol<fuchsia_input_report::InputDevice>(
        &input_report_.value(), "InputReport");
    if (status.is_error()) {
      return status.take_error();
    }
    compat_context_->devfs_exporter().Export(
        std::string("svc/").append(child_->name()), child_->topological_path(), {},
        ZX_PROTOCOL_INPUTREPORT, [this](zx_status_t status) {
          if (status != ZX_OK) {
            FDF_LOG(WARNING, "Failed to export to devfs: %s", zx_status_get_string(status));
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
  std::shared_ptr<compat::Context> compat_context_;
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
