// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.component.decl/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/fidl.h>
#include <fidl/fuchsia.runtime.test/cpp/driver/fidl.h>
#include <fidl/fuchsia.runtime.test/cpp/fidl.h>
#include <lib/driver/component/cpp/driver_cpp.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/protocol.h>
#include <lib/fdf/dispatcher.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <bind/fuchsia/test/cpp/bind.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace fcd = fuchsia_component_decl;
namespace ft = fuchsia_runtime_test;

namespace {

const std::string_view kChildName = "leaf";

class RootDriver : public driver::DriverBase,
                   public fdf::Server<ft::Setter>,
                   public fdf::Server<ft::Getter> {
 public:
  RootDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : DriverBase("root", std::move(start_args), std::move(driver_dispatcher)),
        node_(fidl::WireClient(std::move(node()), dispatcher())) {}

  zx::result<> Start() override {
    driver::ServiceInstanceHandler handler;
    ft::Service::Handler service(&handler);

    auto setter = [this](fdf::ServerEnd<ft::Setter> server_end) mutable -> void {
      fdf::BindServer<fdf::Server<ft::Setter>>(driver_dispatcher()->get(), std::move(server_end),
                                               this);
    };
    zx::result<> status = service.add_setter(std::move(setter));
    if (status.is_error()) {
      FDF_LOG(ERROR, "Failed to add device %s", status.status_string());
    }
    auto getter = [this](fdf::ServerEnd<ft::Getter> server_end) mutable -> void {
      fdf::BindServer<fdf::Server<ft::Getter>>(driver_dispatcher()->get(), std::move(server_end),
                                               this);
    };
    status = service.add_getter(std::move(getter));
    if (status.is_error()) {
      FDF_LOG(ERROR, "Failed to add device %s", status.status_string());
    }
    status = context().outgoing()->AddService<ft::Service>(std::move(handler), kChildName);
    if (status.is_error()) {
      FDF_LOG(ERROR, "Failed to add service %s", status.status_string());
    }

    auto result = AddChild();
    if (result.is_error()) {
      return zx::error(ZX_ERR_INTERNAL);
    }
    return zx::ok();
  }

  // fdf::Server<ft::Setter>
  void Set(SetRequest& request, SetCompleter::Sync& completer) override {
    child_value_ = request.wrapped_value().value();
    completer.Reply(fit::ok());
  }

  // fdf::Server<ft::Getter>
  void Get(GetCompleter::Sync& completer) override {
    ZX_ASSERT(child_value_.has_value());
    completer.Reply(fit::ok(child_value_.value()));
  }

 private:
  fit::result<fdf::wire::NodeError> AddChild() {
    fidl::Arena arena;

    auto offer = driver::MakeOffer<ft::Service>(kChildName);

    // Set the properties of the node that a driver will bind to.
    auto property = fdf::NodeProperty{
        {.key = fdf::NodePropertyKey::WithIntValue(1 /* BIND_PROTOCOL */),
         .value = fdf::NodePropertyValue::WithIntValue(bind_fuchsia_test::BIND_PROTOCOL_DEVICE)}};

    auto args = fdf::NodeAddArgs{{
        .name = std::string(kChildName),
        .offers = std::vector{std::move(offer)},
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

  // Value set by child driver via the |Setter| protocol.
  std::optional<uint32_t> child_value_ = 0;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V3(driver::Record<RootDriver>);
