// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.component.decl/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.test/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>
#include <lib/service/llcpp/outgoing_directory.h>

#include <bind/fuchsia/test/cpp/fidl.h>

#include "src/devices/lib/driver2/logger.h"
#include "src/devices/lib/driver2/promise.h"
#include "src/devices/lib/driver2/record.h"

namespace fcd = fuchsia_component_decl;
namespace fdf = fuchsia_driver_framework;
namespace ft = fuchsia_test;

using fpromise::error;
using fpromise::ok;
using fpromise::promise;
using fpromise::result;

namespace {

class RootDriver : public fidl::WireServer<ft::Handshake> {
 public:
  explicit RootDriver(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher), executor_(dispatcher), outgoing_(dispatcher) {}

  zx::status<> Start(fdf::wire::DriverStartArgs* start_args) {
    // Bind the node.
    node_.Bind(std::move(start_args->node()), dispatcher_);

    // Create the namespace.
    auto ns = driver::Namespace::Create(start_args->ns());
    if (ns.is_error()) {
      return ns.take_error();
    }
    ns_ = std::move(*ns);

    // Create the logger.
    auto logger = driver::Logger::Create(ns_, dispatcher_, "root");
    if (logger.is_error()) {
      return logger.take_error();
    }
    logger_ = std::move(*logger);

    // Setup the outgoing directory.
    auto service = [this](fidl::ServerEnd<ft::Handshake> server_end) {
      fidl::BindServer(dispatcher_, std::move(server_end), this);
      return ZX_OK;
    };
    zx_status_t status = outgoing_.svc_dir()->AddEntry(
        fidl::DiscoverableProtocolName<ft::Handshake>, fbl::MakeRefCounted<fs::Service>(service));
    if (status != ZX_OK) {
      return zx::error(status);
    }
    auto serve = outgoing_.Serve(std::move(start_args->outgoing_dir()));
    if (serve.is_error()) {
      return serve.take_error();
    }

    // Start the driver.
    auto start_driver =
        AddChild().or_else(fit::bind_member(this, &RootDriver::UnbindNode)).wrap_with(scope_);
    executor_.schedule_task(std::move(start_driver));
    return zx::ok();
  }

 private:
  promise<void, zx_status_t> AddChild() {
    fidl::Arena arena;

    // Offer `fuchsia.test.Handshake` to the driver that binds to the node.
    fcd::wire::OfferProtocol protocol(arena);
    protocol.set_source_name(
        arena, fidl::StringView::FromExternal(fidl::DiscoverableProtocolName<ft::Handshake>));
    protocol.set_target_name(
        arena, fidl::StringView::FromExternal(fidl::DiscoverableProtocolName<ft::Handshake>));
    protocol.set_dependency_type(fcd::wire::DependencyType::kStrong);
    fcd::wire::Offer offer;
    offer.set_protocol(arena, std::move(protocol));

    // Set the properties of the node that a driver will bind to.
    fdf::wire::NodeProperty property(arena);
    property.set_key(arena, fdf::wire::NodePropertyKey::WithIntValue(1 /* BIND_PROTOCOL */))
        .set_value(arena, fdf::wire::NodePropertyValue::WithIntValue(
                              bind::fuchsia::test::BIND_PROTOCOL_DEVICE));

    fdf::wire::NodeAddArgs args(arena);
    args.set_name(arena, "leaf")
        .set_offers(arena, fidl::VectorView<fcd::wire::Offer>::FromExternal(&offer, 1))
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

  // fidl::WireServer<ft::Handshake>
  void Do(DoRequestView request, DoCompleter::Sync& completer) override { completer.Reply(); }

  async_dispatcher_t* const dispatcher_;
  async::Executor executor_;
  service::OutgoingDirectory outgoing_;

  fidl::WireSharedClient<fdf::Node> node_;
  fidl::WireSharedClient<fdf::NodeController> controller_;
  driver::Namespace ns_;
  driver::Logger logger_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

zx_status_t DriverStart(fidl_incoming_msg_t* msg, async_dispatcher_t* dispatcher, void** driver) {
  fidl::DecodedMessage<fdf::wire::DriverStartArgs> decoded(msg);
  if (!decoded.ok()) {
    return decoded.status();
  }

  auto root_driver = std::make_unique<RootDriver>(dispatcher);
  auto start = root_driver->Start(decoded.PrimaryObject());
  if (start.is_error()) {
    return start.error_value();
  }

  *driver = root_driver.release();
  return ZX_OK;
}

zx_status_t DriverStop(void* driver) {
  delete static_cast<RootDriver*>(driver);
  return ZX_OK;
}

}  // namespace

FUCHSIA_DRIVER_RECORD_V1(.start = DriverStart, .stop = DriverStop);
