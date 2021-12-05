// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>

#include <bind/fuchsia/test/cpp/fidl.h>

#include "src/devices/lib/driver2/logger.h"
#include "src/devices/lib/driver2/namespace.h"
#include "src/devices/lib/driver2/promise.h"
#include "src/devices/lib/driver2/record_cpp.h"

namespace fdf = fuchsia_driver_framework;

using fpromise::error;
using fpromise::ok;
using fpromise::promise;
using fpromise::result;

namespace {

class RootDriver {
 public:
  RootDriver(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf::Node> node,
             driver::Namespace ns, driver::Logger logger)
      : dispatcher_(dispatcher),
        executor_(dispatcher),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)) {}

  static constexpr const char* Name() { return "root"; }

  static zx::status<std::unique_ptr<RootDriver>> Start(fdf::wire::DriverStartArgs& start_args,
                                                       async_dispatcher_t* dispatcher,
                                                       fidl::WireSharedClient<fdf::Node> node,
                                                       driver::Namespace ns,
                                                       driver::Logger logger) {
    auto driver =
        std::make_unique<RootDriver>(dispatcher, std::move(node), std::move(ns), std::move(logger));
    auto result = driver->Run();
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx::status<> Run() {
    // Start the driver.
    auto task =
        AddChild().or_else(fit::bind_member(this, &RootDriver::UnbindNode)).wrap_with(scope_);
    executor_.schedule_task(std::move(task));
    return zx::ok();
  }

  promise<void, zx_status_t> AddChild() {
    fidl::Arena arena;

    // Set the properties of the node that a driver will bind to.
    fdf::wire::NodeProperty property(arena);
    property.set_key(arena, fdf::wire::NodePropertyKey::WithIntValue(1 /* BIND_PROTOCOL */))
        .set_value(arena, fdf::wire::NodePropertyValue::WithIntValue(
                              bind::fuchsia::test::BIND_PROTOCOL_COMPAT_CHILD));

    fdf::wire::NodeAddArgs args(arena);
    args.set_name(arena, "v1")
        .set_properties(arena,
                        fidl::VectorView<fdf::wire::NodeProperty>::FromExternal(&property, 1));

    // Create endpoints of the `NodeController` for the node.
    auto endpoints = fidl::CreateEndpoints<fdf::NodeController>();
    if (endpoints.is_error()) {
      return fpromise::make_error_promise(endpoints.error_value());
    }

    fpromise::bridge<void, zx_status_t> bridge;
    auto callback = [this, completer = std::move(bridge.completer),
                     client = std::move(endpoints->client)](
                        fidl::WireUnownedResult<fdf::Node::AddChild>& result) mutable {
      if (!result.ok()) {
        completer.complete_error(result.status());
        return;
      }
      if (result->result.is_err()) {
        completer.complete_error(ZX_ERR_INTERNAL);
        return;
      }
      controller_.Bind(std::move(client), dispatcher_);
      completer.complete_ok();
    };
    node_->AddChild(std::move(args), std::move(endpoints->server), {}, std::move(callback));
    return bridge.consumer.promise_or(error(ZX_ERR_UNAVAILABLE));
  }

  result<> UnbindNode(const zx_status_t& status) {
    FDF_LOG(ERROR, "Failed to start root driver: %s", zx_status_get_string(status));
    node_.AsyncTeardown();
    return ok();
  }

  async_dispatcher_t* const dispatcher_;
  async::Executor executor_;

  fidl::WireSharedClient<fdf::Node> node_;
  fidl::WireSharedClient<fdf::NodeController> controller_;
  driver::Namespace ns_;
  driver::Logger logger_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(RootDriver);
