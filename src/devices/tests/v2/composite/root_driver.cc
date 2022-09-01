// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.composite.test/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/node_add_args.h>
#include <lib/driver2/promise.h>
#include <lib/driver2/record_cpp.h>
#include <lib/fpromise/scope.h>

#include <bind/fuchsia/test/cpp/bind.h>

#include "src/devices/lib/compat/compat.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace fcd = fuchsia_component_decl;
namespace fio = fuchsia_io;
namespace ft = fuchsia_composite_test;

using fpromise::error;
using fpromise::ok;
using fpromise::promise;
using fpromise::result;

namespace {

// Name these differently than what the child expects, so we test that
// FDF renames these correctly.
const std::string_view kLeftName = "left-node";
const std::string_view kRightName = "right-node";

class NumberServer : public fidl::WireServer<ft::Device> {
 public:
  explicit NumberServer(uint32_t number) : number_(number) {}

  void GetNumber(GetNumberCompleter::Sync& completer) override { completer.Reply(number_); }

 private:
  uint32_t number_;
};

class RootDriver {
 public:
  RootDriver(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf::Node> node,
             driver::Namespace ns, driver::Logger logger, component::OutgoingDirectory outgoing)
      : dispatcher_(dispatcher),
        executor_(dispatcher),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)),
        outgoing_(std::move(outgoing)) {}

  static constexpr const char* Name() { return "root"; }

  static zx::status<std::unique_ptr<RootDriver>> Start(fdf::wire::DriverStartArgs& start_args,
                                                       fdf::UnownedDispatcher dispatcher,
                                                       fidl::WireSharedClient<fdf::Node> node,
                                                       driver::Namespace ns,
                                                       driver::Logger logger) {
    auto outgoing = component::OutgoingDirectory::Create(dispatcher->async_dispatcher());
    auto driver =
        std::make_unique<RootDriver>(dispatcher->async_dispatcher(), std::move(node), std::move(ns),
                                     std::move(logger), std::move(outgoing));

    auto serve = driver->outgoing_.Serve(std::move(start_args.outgoing_dir()));
    if (serve.is_error()) {
      return serve.take_error();
    }

    auto status = driver->Run();
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx::status<> Run() {
    vfs_.SetDispatcher(dispatcher_);
    service_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();

    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    zx_status_t serve_status = vfs_.Serve(service_dir_, endpoints->server.TakeChannel(),
                                          fs::VnodeConnectionOptions::ReadWrite());
    if (serve_status != ZX_OK) {
      return zx::error(serve_status);
    }

    // Add service "left".
    {
      component::ServiceHandler handler;
      ft::Service::Handler service(&handler);
      auto device = [this](fidl::ServerEnd<ft::Device> server_end) mutable -> void {
        fidl::BindServer<fidl::WireServer<ft::Device>>(dispatcher_, std::move(server_end),
                                                       &this->left_server_);
      };
      zx::status<> status = service.add_device(std::move(device));
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add device %s", status.status_string());
      }
      status = outgoing_.AddService<ft::Service>(std::move(handler), kLeftName);
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add service %s", status.status_string());
      }
    }

    // Add service "right".
    {
      component::ServiceHandler handler;
      ft::Service::Handler service(&handler);
      auto device = [this](fidl::ServerEnd<ft::Device> server_end) mutable -> void {
        fidl::BindServer<fidl::WireServer<ft::Device>>(dispatcher_, std::move(server_end),
                                                       &this->right_server_);
      };
      zx::status<> status = service.add_device(std::move(device));
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add device %s", status.status_string());
      }
      status = outgoing_.AddService<ft::Service>(std::move(handler), kRightName);
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add service %s", status.status_string());
      }
    }

    // Start the driver.
    auto task = AddChild(kLeftName, bind_fuchsia_test::BIND_PROTOCOL_DEVICE, left_controller_)
                    .and_then(AddChild(kRightName, bind_fuchsia_test::BIND_PROTOCOL_POWER_CHILD,
                                       right_controller_))
                    .or_else(fit::bind_member(this, &RootDriver::UnbindNode))
                    .wrap_with(scope_);
    executor_.schedule_task(std::move(task));
    return zx::ok();
  }

  promise<void, fdf::wire::NodeError> AddChild(
      std::string_view name, int protocol,
      fidl::WireSharedClient<fdf::NodeController>& controller) {
    fidl::Arena arena;

    // Set the properties of the node that a driver will bind to.
    fdf::wire::NodeProperty property(arena);
    property.set_key(arena, fdf::wire::NodePropertyKey::WithIntValue(1 /* BIND_PROTOCOL */))
        .set_value(arena, fdf::wire::NodePropertyValue::WithIntValue(protocol));

    fdf::wire::NodeAddArgs args(arena);

    // Set the offers of the node.
    auto offers = fidl::VectorView<fcd::wire::Offer>(arena, 1);
    offers[0] = driver::MakeOffer<ft::Service>(arena, name);

    args.set_offers(arena, offers);

    args.set_name(arena, fidl::StringView::FromExternal(name))
        .set_properties(arena,
                        fidl::VectorView<fdf::wire::NodeProperty>::FromExternal(&property, 1));

    // Create endpoints of the `NodeController` for the node.
    auto endpoints = fidl::CreateEndpoints<fdf::NodeController>();
    if (endpoints.is_error()) {
      return fpromise::make_error_promise(fdf::wire::NodeError::kInternal);
    }

    return driver::AddChild(node_, std::move(args), std::move(endpoints->server), {})
        .and_then([this, &controller, client = std::move(endpoints->client)]() mutable {
          controller.Bind(std::move(client), dispatcher_);
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
  fidl::WireSharedClient<fdf::NodeController> left_controller_;
  fidl::WireSharedClient<fdf::NodeController> right_controller_;
  driver::Namespace ns_;
  driver::Logger logger_;

  NumberServer left_server_ = NumberServer(1);
  NumberServer right_server_ = NumberServer(2);

  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> service_dir_;

  component::OutgoingDirectory outgoing_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(RootDriver);
