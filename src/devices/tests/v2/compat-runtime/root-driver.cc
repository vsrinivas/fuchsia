// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.compat.runtime.test/cpp/driver/fidl.h>
#include <fidl/fuchsia.component.decl/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/fidl.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/driver2/outgoing_directory.h>
#include <lib/driver_compat/compat.h>
#include <lib/driver_compat/symbols.h>

#include <bind/fuchsia/test/cpp/bind.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace ft = fuchsia_compat_runtime_test;

namespace {

class RootDriver : public driver::DriverBase, public fdf::Server<ft::Root> {
 public:
  RootDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : DriverBase("root", std::move(start_args), std::move(driver_dispatcher)),
        node_(fidl::WireClient(std::move(node()), dispatcher())) {}

  static constexpr const char* Name() { return "root"; }

  zx::result<> Start() override {
    // Since our child is a V1 driver, we need to serve a VFS to pass to the |compat::DeviceServer|.
    zx_status_t status = ServeRuntimeProtocolForV1();
    if (status != ZX_OK) {
      return zx::error(status);
    }

    // Start the driver.
    auto result = AddChild();
    if (result.is_error()) {
      return zx::error(ZX_ERR_INTERNAL);
    }
    return zx::ok();
  }

  // fdf::Server<ft::Root>
  void GetString(GetStringCompleter::Sync& completer) override {
    char str[100];
    strcpy(str, "hello world!");
    completer.Reply(std::string(str));
  }

 private:
  zx_status_t ServeRuntimeProtocolForV1() {
    driver::ServiceInstanceHandler handler;
    ft::Service::Handler service(&handler);

    auto root = [this](fdf::ServerEnd<ft::Root> server_end) {
      fdf::BindServer<fdf::Server<ft::Root>>(driver_dispatcher()->get(), std::move(server_end),
                                             this);
    };
    auto status = service.add_root(std::move(root));
    if (status.is_error()) {
      return status.status_value();
    }
    status = context().outgoing()->AddService<ft::Service>(std::move(handler));
    if (status.is_error()) {
      return status.status_value();
    }
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }
    auto serve = context().outgoing()->Serve(
        fidl::ServerEnd<fuchsia_io::Directory>(endpoints->server.TakeChannel()));
    if (serve.is_error()) {
      return serve.status_value();
    }

    vfs_client_ = fidl::ClientEnd<fuchsia_io::Directory>(endpoints->client.TakeChannel());

    return ZX_OK;
  }

  fit::result<fdf::NodeError> AddChild() {
    std::vector<std::string> service_offers;
    service_offers.push_back(std::string(ft::Service::Name));

    child_ = compat::DeviceServer(
        "v1", 0, "root/v1",
        compat::ServiceOffersV1("v1", std::move(vfs_client_), std::move(service_offers)));
    zx_status_t status = child_->Serve(dispatcher(), context().outgoing().get());
    if (status != ZX_OK) {
      return fit::error(fdf::NodeError::kInternal);
    }

    fidl::Arena arena;

    // Set the symbols of the node that a driver will have access to.
    compat_device_.name = "v1";
    auto symbol = fdf::NodeSymbol{
        {.name = compat::kDeviceSymbol, .address = reinterpret_cast<uint64_t>(&compat_device_)}};

    // Set the properties of the node that a driver will bind to.
    auto property =
        driver::MakeProperty(1 /*BIND_PROTOCOL */, bind_fuchsia_test::BIND_PROTOCOL_COMPAT_CHILD);

    auto offers = child_->CreateOffers(arena);
    std::vector<fuchsia_component_decl::Offer> natural_offers;
    for (auto offer : offers) {
      natural_offers.push_back(fidl::ToNatural(offer));
    }
    auto args = fdf::NodeAddArgs{{
        .name = std::string("v1"),
        .offers = std::move(natural_offers),
        .symbols = std::vector{std::move(symbol)},
        .properties = std::vector{std::move(property)},
    }};

    // Create endpoints of the `NodeController` for the node.
    auto endpoints = fidl::CreateEndpoints<fdf::NodeController>();
    if (endpoints.is_error()) {
      return fit::error(fdf::NodeError::kInternal);
    }

    auto add_result =
        node_.sync()->AddChild(fidl::ToWire(arena, args), std::move(endpoints->server), {});
    if (!add_result.ok()) {
      return fit::error(fdf::NodeError::kInternal);
    }
    if (add_result->is_error()) {
      return fit::error(add_result->error_value());
    }
    controller_.Bind(std::move(endpoints->client), dispatcher());
    return fit::ok();
  }

  fidl::WireClient<fdf::Node> node_;
  fidl::WireSharedClient<fdf::NodeController> controller_;

  compat::device_t compat_device_ = compat::kDefaultDevice;
  std::optional<compat::DeviceServer> child_;
  fidl::ClientEnd<fuchsia_io::Directory> vfs_client_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<RootDriver>);
