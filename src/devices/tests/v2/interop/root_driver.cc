// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/ddk/device.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>

#include <bind/fuchsia/test/cpp/fidl.h>

#include "lib/fpromise/result.h"
#include "src/devices/lib/compat/compat.h"
#include "src/devices/lib/compat/symbols.h"
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
        logger_(std::move(logger)),
        outgoing_(dispatcher) {}

  static constexpr const char* Name() { return "root"; }

  static zx::status<std::unique_ptr<RootDriver>> Start(fdf::wire::DriverStartArgs& start_args,
                                                       async_dispatcher_t* dispatcher,
                                                       fidl::WireSharedClient<fdf::Node> node,
                                                       driver::Namespace ns,
                                                       driver::Logger logger) {
    auto driver =
        std::make_unique<RootDriver>(dispatcher, std::move(node), std::move(ns), std::move(logger));

    auto serve = driver->outgoing_.Serve(std::move(start_args.outgoing_dir()));
    if (serve.is_error()) {
      return serve.take_error();
    }
    auto result = driver->Run();
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx::status<> Run() {
    auto interop = compat::Interop::Create(dispatcher_, &ns_, &outgoing_);
    if (interop.is_error()) {
      return interop.take_error();
    }
    interop_ = std::move(*interop);

    // Start the driver.
    auto task =
        AddChild().or_else(fit::bind_member(this, &RootDriver::UnbindNode)).wrap_with(scope_);
    executor_.schedule_task(std::move(task));
    return zx::ok();
  }

  promise<void, fdf::wire::NodeError> AddChild() {
    child_ = compat::Child("v1", 0, "root/v1", nullptr, compat::MetadataMap());
    return interop_.ExportChild(&child_.value())
        .or_else([](const zx_status_t& status) {
          return fpromise::error(fdf::wire::NodeError::kInternal);
        })
        .and_then([this]() mutable {
          fidl::Arena arena;

          // Set the symbols of the node that a driver will have access to.
          compat_device_.name = "v1";
          compat_device_.proto_ops.ops = reinterpret_cast<void*>(0xabcdef);
          fdf::wire::NodeSymbol symbol(arena);
          symbol.set_name(arena, compat::kDeviceSymbol)
              .set_address(arena, reinterpret_cast<uint64_t>(&compat_device_));

          // Set the properties of the node that a driver will bind to.
          fdf::wire::NodeProperty property(arena);
          property.set_key(arena, fdf::wire::NodePropertyKey::WithIntValue(1 /* BIND_PROTOCOL */))
              .set_value(arena, fdf::wire::NodePropertyValue::WithIntValue(
                                    bind::fuchsia::test::BIND_PROTOCOL_COMPAT_CHILD));
          auto offers = child_->CreateOffers(arena);

          fdf::wire::NodeAddArgs args(arena);
          args.set_name(arena, "v1")
              .set_symbols(arena, fidl::VectorView<fdf::wire::NodeSymbol>::FromExternal(&symbol, 1))
              .set_offers(arena,
                          fidl::VectorView<fuchsia_component_decl::wire::Offer>::FromExternal(
                              offers.data(), offers.size()))
              .set_properties(
                  arena, fidl::VectorView<fdf::wire::NodeProperty>::FromExternal(&property, 1));

          // Create endpoints of the `NodeController` for the node.
          auto endpoints = fidl::CreateEndpoints<fdf::NodeController>();
          if (endpoints.is_error()) {
            return fpromise::make_error_promise(fdf::wire::NodeError::kInternal);
          }

          auto task = driver::AddChild(node_, std::move(args), std::move(endpoints->server), {})
                          .and_then([this, client = std::move(endpoints->client)]() mutable {
                            controller_.Bind(std::move(client), dispatcher_);
                          });
          executor_.schedule_task(std::move(task));
          return fpromise::make_result_promise<void, fdf::wire::NodeError>(fpromise::ok());
        });
  }

  result<> UnbindNode(const fdf::wire::NodeError& error) {
    FDF_LOG(ERROR, "Failed to start root driver: %d", error);
    node_.AsyncTeardown();
    return ok();
  }

  async_dispatcher_t* const dispatcher_;
  async::Executor executor_;

  fidl::WireSharedClient<fdf::Node> node_;
  fidl::WireSharedClient<fdf::NodeController> controller_;
  driver::Namespace ns_;
  driver::Logger logger_;

  zx_protocol_device_t ops_ = {
      .get_protocol = [](void*, uint32_t, void*) { return ZX_OK; },
  };

  compat::Interop interop_;
  compat::device_t compat_device_ = compat::kDefaultDevice;
  std::optional<compat::Child> child_;
  service::OutgoingDirectory outgoing_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(RootDriver);
