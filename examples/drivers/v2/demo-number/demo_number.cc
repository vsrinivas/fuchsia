// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.hardware.demo/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/scope.h>
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

namespace fdf2 = fuchsia_driver_framework;
namespace fio = fuchsia_io;

namespace {

class DemoNumber : public fidl::WireServer<fuchsia_hardware_demo::Demo> {
 public:
  DemoNumber(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf2::Node> node,
             driver::Namespace ns, driver::Logger logger)
      : dispatcher_(dispatcher),
        outgoing_(dispatcher),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)),
        executor_(dispatcher) {}

  static constexpr const char* Name() { return "demo_number"; }

  static zx::status<std::unique_ptr<DemoNumber>> Start(fdf2::wire::DriverStartArgs& start_args,
                                                       async_dispatcher_t* dispatcher,
                                                       fidl::WireSharedClient<fdf2::Node> node,
                                                       driver::Namespace ns,
                                                       driver::Logger logger) {
    auto driver =
        std::make_unique<DemoNumber>(dispatcher, std::move(node), std::move(ns), std::move(logger));

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

    auto parent_client = compat::ConnectToParentDevice(dispatcher_, &ns_);
    if (parent_client.is_error()) {
      FDF_LOG(WARNING, "Connecting to compat service failed with %s",
              zx_status_get_string(parent_client.error_value()));
      return parent_client.take_error();
    }
    parent_client_ = std::move(parent_client.value());

    auto compat_connect =
        fpromise::make_result_promise<void, zx_status_t>(fpromise::ok())
            // Get our parent's topological path.
            .and_then([this]() {
              fpromise::bridge<void, zx_status_t> topo_bridge;
              parent_client_->GetTopologicalPath().Then(
                  [this, completer = std::move(topo_bridge.completer)](
                      fidl::WireUnownedResult<fuchsia_driver_compat::Device::GetTopologicalPath>&
                          result) mutable {
                    if (!result.ok()) {
                      completer.complete_error(result.status());
                      return;
                    }
                    auto* response = result.Unwrap();
                    parent_topo_path_ = std::string(response->path.data(), response->path.size());
                    completer.complete_ok();
                  });
              return topo_bridge.consumer.promise_or(fpromise::error(ZX_ERR_CANCELED));
            })
            // Create our child device and FIDL server.
            .and_then([this]() {
              auto protocol = fbl::MakeRefCounted<fs::Service>([this](zx::channel channel) {
                fidl::BindServer<fidl::WireServer<fuchsia_hardware_demo::Demo>>(
                    dispatcher_, fidl::ServerEnd<fuchsia_hardware_demo::Demo>(std::move(channel)),
                    this);
                return ZX_OK;
              });
              child_ = compat::Child(Name(), 0, parent_topo_path_ + "/" + Name(), {});
              return interop_.ExportChild(&child_.value(), protocol);
            })
            // Error handling.
            .or_else([this](zx_status_t& result) {
              FDF_LOG(WARNING, "Device setup failed with: %s", zx_status_get_string(result));
            });
    executor_.schedule_task(std::move(compat_connect));

    return outgoing_.Serve(std::move(outgoing_dir));
  }

  void GetNumber(GetNumberRequestView request, GetNumberCompleter::Sync& completer) override {
    completer.Reply(current_number);
    current_number += 1;
  }

  async_dispatcher_t* dispatcher_;
  service::OutgoingDirectory outgoing_;
  fidl::WireSharedClient<fdf2::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;
  async::Executor executor_;
  fidl::WireSharedClient<fuchsia_driver_compat::Device> parent_client_;

  compat::Interop interop_;
  std::optional<compat::Child> child_;
  std::string parent_topo_path_;
  uint32_t current_number = 0;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(DemoNumber);
