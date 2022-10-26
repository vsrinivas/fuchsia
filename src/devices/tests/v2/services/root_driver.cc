// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.services.test/cpp/wire.h>
#include <lib/driver2/driver2_cpp.h>

namespace ft = fuchsia_services_test;

namespace {

class RootDriver : public driver::DriverBase,
                   public fidl::WireServer<ft::ControlPlane>,
                   public fidl::WireServer<ft::DataPlane> {
 public:
  RootDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : driver::DriverBase("root", std::move(start_args), std::move(driver_dispatcher)) {}

  zx::result<> Start() override {
    component::ServiceInstanceHandler handler;
    ft::Device::Handler device(&handler);

    auto control = [this](fidl::ServerEnd<ft::ControlPlane> server_end) -> void {
      fidl::BindServer<fidl::WireServer<ft::ControlPlane>>(dispatcher(), std::move(server_end),
                                                           this);
    };
    auto result = device.add_control(control);
    if (result.is_error()) {
      FDF_LOG(ERROR, "Failed to add control handler to Device service: %s", result.status_string());
      return result.take_error();
    }

    auto data = [this](fidl::ServerEnd<ft::DataPlane> server_end) -> void {
      fidl::BindServer<fidl::WireServer<ft::DataPlane>>(dispatcher(), std::move(server_end), this);
    };
    result = device.add_data(data);
    if (result.is_error()) {
      FDF_LOG(ERROR, "Failed to add data handler to Device service: %s", result.status_string());
      return result.take_error();
    }

    result = context().outgoing()->AddService<ft::Device>(std::move(handler));
    if (result.is_error()) {
      FDF_LOG(ERROR, "Failed to add Device service: %s", result.status_string());
      return result.take_error();
    }
    return zx::ok();
  }

 private:
  // fidl::WireServer<ft::ControlPlane>
  void ControlDo(ControlDoCompleter::Sync& completer) override { completer.Reply(); }

  // fidl::WireServer<ft::DataPlane>
  void DataDo(DataDoCompleter::Sync& completer) override { completer.Reply(); }
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<RootDriver>);
