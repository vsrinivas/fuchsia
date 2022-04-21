// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.services.test/cpp/wire.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/record_cpp.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>

namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia_io;
namespace ft = fuchsia_services_test;

namespace {

class RootDriver : public fidl::WireServer<ft::ControlPlane>,
                   public fidl::WireServer<ft::DataPlane> {
 public:
  RootDriver(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf::Node> node,
             driver::Namespace ns, driver::Logger logger)
      : dispatcher_(dispatcher),
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
    component::ServiceHandler handler;
    ft::Device::Handler device(&handler);

    auto control = [this](fidl::ServerEnd<ft::ControlPlane> server_end) -> void {
      fidl::BindServer<fidl::WireServer<ft::ControlPlane>>(dispatcher_, std::move(server_end),
                                                           this);
    };
    auto result = device.add_control(control);
    if (result.is_error()) {
      FDF_LOG(ERROR, "Failed to add control handler to Device service: %s", result.status_string());
      return result.take_error();
    }

    auto data = [this](fidl::ServerEnd<ft::DataPlane> server_end) -> void {
      fidl::BindServer<fidl::WireServer<ft::DataPlane>>(dispatcher_, std::move(server_end), this);
    };
    result = device.add_data(data);
    if (result.is_error()) {
      FDF_LOG(ERROR, "Failed to add data handler to Device service: %s", result.status_string());
      return result.take_error();
    }

    result = outgoing_.AddService<ft::Device>(std::move(handler));
    if (result.is_error()) {
      FDF_LOG(ERROR, "Failed to add Device service: %s", result.status_string());
      return result.take_error();
    }
    return outgoing_.Serve(std::move(outgoing_dir));
  }

  // fidl::WireServer<ft::ControlPlane>
  void ControlDo(ControlDoRequestView request, ControlDoCompleter::Sync& completer) override {
    completer.Reply();
  }

  // fidl::WireServer<ft::DataPlane>
  void DataDo(DataDoRequestView request, DataDoCompleter::Sync& completer) override {
    completer.Reply();
  }

  async_dispatcher_t* dispatcher_;
  component::OutgoingDirectory outgoing_;

  fidl::WireSharedClient<fdf::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(RootDriver);
