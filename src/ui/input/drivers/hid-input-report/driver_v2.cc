// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/ddk/debug.h>
#include <lib/fpromise/scope.h>
#include <lib/service/llcpp/outgoing_directory.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>
#include <zircon/errors.h>

#include "src/devices/lib/compat/compat.h"
#include "src/devices/lib/compat/symbols.h"
#include "src/devices/lib/driver2/devfs_exporter.h"
#include "src/devices/lib/driver2/inspect.h"
#include "src/devices/lib/driver2/namespace.h"
#include "src/devices/lib/driver2/record_cpp.h"
#include "src/devices/lib/driver2/start_args.h"
#include "src/devices/lib/driver2/structured_logger.h"
#include "src/ui/input/drivers/hid-input-report/input-report.h"

namespace fdf2 = fuchsia_driver_framework;
namespace fio = fuchsia_io;

namespace {

class InputReportDriver {
 public:
  InputReportDriver(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf2::Node> node,
                    driver::Namespace ns, driver::Logger logger)
      : dispatcher_(dispatcher),
        outgoing_(dispatcher),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)),
        executor_(dispatcher) {}

  static constexpr const char* Name() { return "InputReport"; }

  static zx::status<std::unique_ptr<InputReportDriver>> Start(
      fdf2::wire::DriverStartArgs& start_args, async_dispatcher_t* dispatcher,
      fidl::WireSharedClient<fdf2::Node> node, driver::Namespace ns, driver::Logger logger) {
    auto driver = std::make_unique<InputReportDriver>(dispatcher, std::move(node), std::move(ns),
                                                      std::move(logger));
    fidl::VectorView<fdf2::wire::NodeSymbol> symbols;
    if (start_args.has_symbols()) {
      symbols = start_args.symbols();
    }

    auto parent_symbol = driver::GetSymbol<compat::device_t*>(symbols, compat::kDeviceSymbol);

    hid_device_protocol_t proto = {};
    if (parent_symbol->proto_ops.id != ZX_PROTOCOL_HID_DEVICE) {
      FDF_LOGL(ERROR, driver->logger_, "Didn't find HID_DEVICE protocol");
      return zx::error(ZX_ERR_NOT_FOUND);
    }
    proto.ctx = parent_symbol->context;
    proto.ops = reinterpret_cast<hid_device_protocol_ops_t*>(parent_symbol->proto_ops.ops);

    ddk::HidDeviceProtocolClient hiddev(&proto);
    if (!hiddev.is_valid()) {
      FDF_LOGL(ERROR, driver->logger_, "Failed to create hiddev");
      return zx::error(ZX_ERR_INTERNAL);
    }
    driver->input_report_.emplace(std::move(hiddev));

    auto result = driver->Run(std::move(start_args.outgoing_dir()));
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx::status<> Run(fidl::ServerEnd<fio::Directory> outgoing_dir) {
    auto interop = compat::Interop::Create(dispatcher_, &ns_, &outgoing_);
    if (interop.is_error()) {
      return interop.take_error();
    }
    interop_ = std::move(*interop);

    input_report_->Start();

    auto compat_connect =
        interop_
            .ConnectToParentCompatService()
            // Get our parent's topological path.
            .and_then([this]() {
              fpromise::bridge<void, zx_status_t> topo_bridge;
              interop_.device_client()->GetTopologicalPath(
                  [this, completer = std::move(topo_bridge.completer)](
                      fidl::WireResponse<fuchsia_driver_compat::Device::GetTopologicalPath>*
                          response) mutable {
                    parent_topo_path_ = std::string(response->path.data(), response->path.size());
                    completer.complete_ok();
                  });
              return topo_bridge.consumer.promise_or(fpromise::error(ZX_ERR_CANCELED));
            })
            // Create our child device and FIDL server.
            .and_then([this]() {
              auto input_protocol = fbl::MakeRefCounted<fs::Service>([this](zx::channel channel) {
                fidl::BindServer<fidl::WireServer<fuchsia_input_report::InputDevice>>(
                    dispatcher_,
                    fidl::ServerEnd<fuchsia_input_report::InputDevice>(std::move(channel)),
                    &input_report_.value());
                return ZX_OK;
              });
              child_ = compat::Child("InputReport", ZX_PROTOCOL_INPUTREPORT,
                                     parent_topo_path_ + "/InputReport", input_protocol, {});
              return interop_.ExportChild(&child_.value());
            })
            // Error handling.
            .or_else([this](zx_status_t& result) {
              FDF_LOG(WARNING, "Device setup failed with: %s", zx_status_get_string(result));
            });
    executor_.schedule_task(std::move(compat_connect));

    // TODO(fxbug.dev/96231): Move compat library to the correct OutgoingDir, and then add inspect
    // data here.

    return outgoing_.Serve(std::move(outgoing_dir));
  }

  async_dispatcher_t* dispatcher_;
  std::optional<hid_input_report_dev::InputReport> input_report_;
  service::OutgoingDirectory outgoing_;
  fidl::WireSharedClient<fdf2::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;
  inspect::Inspector inspector_;
  zx::vmo inspect_vmo_;
  async::Executor executor_;

  compat::Interop interop_;
  std::optional<compat::Child> child_;
  std::string parent_topo_path_;

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

FUCHSIA_DRIVER_RECORD_CPP_V1(InputReportDriver);
