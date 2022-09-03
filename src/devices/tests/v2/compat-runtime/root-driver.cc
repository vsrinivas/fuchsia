// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.compat.runtime.test/cpp/driver/fidl.h>
#include <fidl/fuchsia.component.decl/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/fidl.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/node_add_args.h>
#include <lib/driver2/record_cpp.h>
#include <lib/driver2/runtime.h>
#include <lib/driver2/runtime_connector_impl.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <bind/fuchsia/test/cpp/bind.h>

#include "src/devices/lib/compat/compat.h"
#include "src/devices/lib/compat/symbols.h"

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace ft = fuchsia_compat_runtime_test;

namespace {

class RootDriver : public fdf::Server<ft::Root>, public driver::RuntimeConnectorImpl {
 public:
  RootDriver(fdf::UnownedDispatcher dispatcher, fidl::WireSharedClient<fdf::Node> node,
             driver::Namespace ns, driver::Logger logger, component::OutgoingDirectory outgoing)
      : driver::RuntimeConnectorImpl(dispatcher->async_dispatcher()),
        dispatcher_(dispatcher->async_dispatcher()),
        fdf_dispatcher_(std::move(dispatcher)),
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
        std::make_unique<RootDriver>(std::move(dispatcher), std::move(node), std::move(ns),
                                     std::move(logger), std::move(outgoing));

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

  // Called when a new connection to the ft::Root driver transport protocol is requested.
  zx_status_t OnConnectRoot(fdf::Channel channel) {
    fdf::ServerEnd<ft::Root> server_end(std::move(channel));
    fdf::BindServer<fdf::Server<ft::Root>>(fdf_dispatcher_->get(), std::move(server_end), this);
    return ZX_OK;
  }

  // fdf::Server<ft::Root>
  void GetString(GetStringRequest& request, GetStringCompleter::Sync& completer) override {
    char str[100];
    strcpy(str, "hello world!");
    completer.Reply(std::string(str));
  }

 private:
  zx::status<> Run() {
    // Since our child is a V1 driver, we need to serve a VFS to pass to the |compat::DeviceServer|.
    zx_status_t status = ServeRuntimeProtocolForV1();
    if (status != ZX_OK) {
      return zx::error(status);
    }

    // Start the driver.
    auto result = AddChild();
    if (result.is_error()) {
      UnbindNode(result.error_value());
      return zx::error(ZX_ERR_INTERNAL);
    }
    return zx::ok();
  }

  zx_status_t ServeRuntimeProtocolForV1() {
    // Setup the outgoing directory.
    auto service = [this](fidl::ServerEnd<fdf::RuntimeConnector> server_end) {
      fidl::BindServer<fidl::WireServer<fdf::RuntimeConnector>>(dispatcher_, std::move(server_end),
                                                                this);
    };
    zx::status<> status = outgoing_.AddProtocol<fdf::RuntimeConnector>(std::move(service));
    if (status.is_error()) {
      return status.status_value();
    }
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }
    auto serve =
        outgoing_.Serve(fidl::ServerEnd<fuchsia_io::Directory>(endpoints->server.TakeChannel()));
    if (serve.is_error()) {
      return serve.status_value();
    }

    vfs_client_ = fidl::ClientEnd<fuchsia_io::Directory>(endpoints->client.TakeChannel());

    RegisterProtocol(fidl::DiscoverableProtocolName<ft::Root>,
                     fit::bind_member(this, &RootDriver::OnConnectRoot));
    return ZX_OK;
  }

  fitx::result<fdf::NodeError> AddChild() {
    child_ = compat::DeviceServer("v1", 0, "root/v1", compat::MetadataMap(),
                                  compat::ServiceOffersV1("v1", std::move(vfs_client_), {}));
    zx_status_t status = child_->Serve(dispatcher_, &outgoing_);
    if (status != ZX_OK) {
      return fitx::error(fdf::NodeError::kInternal);
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
      natural_offers.push_back(*fidl::ToNatural(offer));
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
      return fitx::error(fdf::NodeError::kInternal);
    }

    auto add_result =
        node_.sync()->AddChild(fidl::ToWire(arena, args), std::move(endpoints->server), {});
    if (!add_result.ok()) {
      return fitx::error(fdf::NodeError::kInternal);
    }
    if (add_result->is_error()) {
      return fitx::error(add_result->error_value());
    }
    controller_.Bind(std::move(endpoints->client), dispatcher_);
    return fitx::ok();
  }

  void UnbindNode(const fdf::NodeError& error) {
    FDF_LOG(ERROR, "Failed to start root driver: %d", error);
    node_.AsyncTeardown();
  }

  async_dispatcher_t* const dispatcher_;
  fdf::UnownedDispatcher fdf_dispatcher_;

  fidl::WireSharedClient<fdf::Node> node_;
  fidl::WireSharedClient<fdf::NodeController> controller_;
  driver::Namespace ns_;
  driver::Logger logger_;

  component::OutgoingDirectory outgoing_;
  compat::device_t compat_device_ = compat::kDefaultDevice;
  std::optional<compat::DeviceServer> child_;
  fidl::ClientEnd<fuchsia_io::Directory> vfs_client_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(RootDriver);
