// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.composite.test/cpp/wire.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/driver2/service_client.h>
#include <lib/driver_compat/compat.h>

#include <bind/fuchsia/test/cpp/bind.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace fcd = fuchsia_component_decl;
namespace ft = fuchsia_composite_test;

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

class RootDriver : public driver::DriverBase {
 public:
  RootDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : driver::DriverBase("root", std::move(start_args), std::move(driver_dispatcher)) {}

  zx::result<> Start() override {
    node_client_.Bind(std::move(node()), dispatcher());
    // Add service "left".
    {
      component::ServiceInstanceHandler handler;
      ft::Service::Handler service(&handler);
      auto device = [this](fidl::ServerEnd<ft::Device> server_end) mutable -> void {
        fidl::BindServer<fidl::WireServer<ft::Device>>(dispatcher(), std::move(server_end),
                                                       &this->left_server_);
      };
      zx::result<> status = service.add_device(std::move(device));
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add device %s", status.status_string());
      }
      status = context().outgoing()->AddService<ft::Service>(std::move(handler), kLeftName);
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add service %s", status.status_string());
      }
    }

    // Add service "right".
    {
      component::ServiceInstanceHandler handler;
      ft::Service::Handler service(&handler);
      auto device = [this](fidl::ServerEnd<ft::Device> server_end) mutable -> void {
        fidl::BindServer<fidl::WireServer<ft::Device>>(dispatcher(), std::move(server_end),
                                                       &this->right_server_);
      };
      zx::result<> status = service.add_device(std::move(device));
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add device %s", status.status_string());
      }
      status = context().outgoing()->AddService<ft::Service>(std::move(handler), kRightName);
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add service %s", status.status_string());
      }
    }

    auto success = StartChildren();
    if (!success) {
      DropNode();
      return zx::error(ZX_ERR_INTERNAL);
    }

    return zx::ok();
  }

 private:
  bool StartChildren() {
    auto left = AddChild(kLeftName, bind_fuchsia_test::BIND_PROTOCOL_DEVICE, left_controller_);
    if (!left) {
      FDF_LOG(ERROR, "Failed to start left child.");
      return false;
    }

    auto right =
        AddChild(kRightName, bind_fuchsia_test::BIND_PROTOCOL_POWER_CHILD, right_controller_);
    if (!right) {
      FDF_LOG(ERROR, "Failed to start right child.");
      return false;
    }

    return true;
  }

  bool AddChild(std::string_view name, int protocol,
                fidl::WireSharedClient<fdf::NodeController>& controller) {
    fidl::Arena arena;

    // Set the properties of the node that a driver will bind to.
    fdf::wire::NodeProperty property = driver::MakeProperty(arena, 1 /* BIND_PROTOCOL */, protocol);

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
      return false;
    }

    auto add_callback = [this, &controller, client = std::move(endpoints->client)](
                            fidl::WireUnownedResult<fdf::Node::AddChild>& result) mutable {
      if (!result.ok()) {
        FDF_LOG(ERROR, "Adding child failed: %s", result.error().status_string());
        DropNode();
        return;
      }

      if (result->is_error()) {
        FDF_LOG(ERROR, "Adding child failed: %d", result->error_value());
        DropNode();
        return;
      }

      controller.Bind(std::move(client), dispatcher());
      FDF_LOG(INFO, "Successfully added child.");
    };

    node_client_->AddChild(args, std::move(endpoints->server), {}).Then(std::move(add_callback));
    return true;
  }

  void DropNode() { node_client_.AsyncTeardown(); }

  fidl::WireSharedClient<fdf::NodeController> left_controller_;
  fidl::WireSharedClient<fdf::NodeController> right_controller_;

  fidl::WireSharedClient<fdf::Node> node_client_;

  NumberServer left_server_ = NumberServer(1);
  NumberServer right_server_ = NumberServer(2);
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<RootDriver>);
