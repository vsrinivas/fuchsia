// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.component.decl/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.offers.test/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/promise.h>
#include <lib/driver2/record_cpp.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>

#include <bind/fuchsia/test/cpp/fidl.h>

namespace fcd = fuchsia_component_decl;
namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia_io;
namespace ft = fuchsia_offers_test;

using fpromise::error;
using fpromise::ok;
using fpromise::promise;
using fpromise::result;

namespace {

class RootDriver : public fidl::WireServer<ft::Handshake> {
 public:
  RootDriver(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf::Node> node,
             driver::Namespace ns, driver::Logger logger)
      : dispatcher_(dispatcher),
        executor_(dispatcher),
        outgoing_(component::OutgoingDirectory::Create(dispatcher)),
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
    auto result = driver->Run(std::move(start_args.outgoing_dir()));
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx::status<> Run(fidl::ServerEnd<fio::Directory> outgoing_dir) {
    // Setup the outgoing directory.
    auto service = [this](fidl::ServerEnd<ft::Handshake> server_end) {
      fidl::BindServer(dispatcher_, std::move(server_end), this);
    };
    zx::status<> status = outgoing_.AddProtocol<ft::Handshake>(std::move(service));
    if (status.is_error()) {
      return status;
    }

    auto serve = outgoing_.Serve(std::move(outgoing_dir));
    if (serve.is_error()) {
      return serve.take_error();
    }

    // Start the driver.
    auto task =
        AddChild().or_else(fit::bind_member(this, &RootDriver::UnbindNode)).wrap_with(scope_);
    executor_.schedule_task(std::move(task));
    return zx::ok();
  }

  promise<void, fdf::wire::NodeError> AddChild() {
    fidl::Arena arena;

    // Offer `fuchsia.test.Handshake` to the driver that binds to the node.
    fcd::wire::OfferProtocol protocol(arena);
    protocol.set_source_name(
        arena, fidl::StringView::FromExternal(fidl::DiscoverableProtocolName<ft::Handshake>));
    protocol.set_target_name(
        arena, fidl::StringView::FromExternal(fidl::DiscoverableProtocolName<ft::Handshake>));
    protocol.set_dependency_type(fcd::wire::DependencyType::kStrong);
    fcd::wire::Offer offer = fcd::wire::Offer::WithProtocol(arena, std::move(protocol));

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
      return fpromise::make_error_promise(fdf::wire::NodeError::kInternal);
    }

    return driver::AddChild(node_, std::move(args), std::move(endpoints->server), {})
        .and_then([this, client = std::move(endpoints->client)]() mutable {
          controller_.Bind(std::move(client), dispatcher_);
        });
  }

  result<> UnbindNode(const fdf::wire::NodeError& error) {
    FDF_LOG(ERROR, "Failed to start root driver: %d", error);
    node_.AsyncTeardown();
    return ok();
  }

  // fidl::WireServer<ft::Handshake>
  void Do(DoRequestView request, DoCompleter::Sync& completer) override { completer.Reply(); }

  async_dispatcher_t* const dispatcher_;
  async::Executor executor_;
  component::OutgoingDirectory outgoing_;

  fidl::WireSharedClient<fdf::Node> node_;
  fidl::WireSharedClient<fdf::NodeController> controller_;
  driver::Namespace ns_;
  driver::Logger logger_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(RootDriver);
