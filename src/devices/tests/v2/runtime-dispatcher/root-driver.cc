// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.component.decl/cpp/fidl.h>
#include <fidl/fuchsia.runtime.test/cpp/fidl.h>
#include <lib/driver2/driver2_cpp.h>

#include <bind/fuchsia/test/cpp/bind.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace fcd = fuchsia_component_decl;
namespace ft = fuchsia_runtime_test;

namespace {

class RootDriver : public driver::DriverBase, public fidl::WireServer<ft::Handshake> {
 public:
  RootDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : driver::DriverBase("root", std::move(start_args), std::move(driver_dispatcher)) {}

  zx::result<> Start() override {
    node_.Bind(std::move(node()), dispatcher());
    // Setup the outgoing directory.
    auto service = [this](fidl::ServerEnd<ft::Handshake> server_end) {
      fidl::BindServer(dispatcher(), std::move(server_end), this);
    };
    zx::result<> status =
        context().outgoing()->component().AddProtocol<ft::Handshake>(std::move(service));
    if (status.is_error()) {
      return status;
    }

    // Offer `fuchsia.test.Handshake` to the driver that binds to the node.
    auto offer =
        fcd::Offer::WithProtocol({{.source_name = fidl::DiscoverableProtocolName<ft::Handshake>,
                                   .target_name = fidl::DiscoverableProtocolName<ft::Handshake>,
                                   .dependency_type = fcd::DependencyType::kStrong}});

    // Set the properties of the node that a driver will bind to.
    auto property =
        driver::MakeProperty(1 /* BIND_PROTOCOL */, bind_fuchsia_test::BIND_PROTOCOL_DEVICE);

    auto args = fdf::NodeAddArgs{{
        .name = "leaf",
        .offers = {{offer}},
        .properties = {{property}},
    }};

    // Create endpoints of the `NodeController` for the node.
    auto endpoints = fidl::CreateEndpoints<fdf::NodeController>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }

    node_->AddChild({std::move(args), std::move(endpoints->server), {}})
        .Then([this, client = std::move(endpoints->client)](
                  fidl::Result<fdf::Node::AddChild>& add_result) mutable {
          if (add_result.is_error()) {
            FDF_LOG(ERROR, "Failed to AddChild: %s",
                    add_result.error_value().FormatDescription().c_str());
            node_.AsyncTeardown();
            return;
          }

          controller_.Bind(std::move(client), dispatcher());
        });

    return zx::ok();
  }

 private:
  // fidl::WireServer<ft::Handshake>
  void Do(DoCompleter::Sync& completer) override { completer.Reply(); }

  fidl::SharedClient<fdf::Node> node_;
  fidl::SharedClient<fdf::NodeController> controller_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<RootDriver>);
