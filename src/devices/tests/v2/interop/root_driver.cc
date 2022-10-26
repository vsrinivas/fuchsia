// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/driver_compat/compat.h>
#include <lib/driver_compat/symbols.h>

#include <bind/fuchsia/test/cpp/bind.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace {

class RootDriver : public driver::DriverBase {
 public:
  RootDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : driver::DriverBase("root", std::move(start_args), std::move(driver_dispatcher)) {}

  zx::result<> Start() override {
    node_.Bind(std::move(node()), dispatcher());
    child_ = compat::DeviceServer("v1", 0, "root/v1");
    auto status = child_->Serve(dispatcher(), &context().outgoing()->component());
    if (status != ZX_OK) {
      FDF_LOG(ERROR, "Failed to serve compat device server: %s", zx_status_get_string(status));
      node_.AsyncTeardown();
      return zx::error(status);
    }

    // Set the symbols of the node that a driver will have access to.
    compat_device_.name = "v1";
    compat_device_.proto_ops.ops = reinterpret_cast<void*>(0xabcdef);

    fdf::NodeSymbol symbol(
        {.name = compat::kDeviceSymbol, .address = reinterpret_cast<uint64_t>(&compat_device_)});

    // Set the properties of the node that a driver will bind to.
    fdf::NodeProperty property =
        driver::MakeProperty(1 /* BIND_PROTOCOL */, bind_fuchsia_test::BIND_PROTOCOL_COMPAT_CHILD);

    auto offers = child_->CreateOffers();

    fdf::NodeAddArgs args(
        {.name = "v1", .offers = offers, .symbols = {{symbol}}, .properties = {{property}}});

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
  fidl::SharedClient<fdf::Node> node_;
  fidl::SharedClient<fdf::NodeController> controller_;

  zx_protocol_device_t ops_ = {
      .get_protocol = [](void*, uint32_t, void*) { return ZX_OK; },
  };

  compat::device_t compat_device_ = compat::kDefaultDevice;
  std::optional<compat::DeviceServer> child_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V3(driver::Record<RootDriver>);
