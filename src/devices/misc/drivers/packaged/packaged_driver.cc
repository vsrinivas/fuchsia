// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>

#include <optional>

#include "src/devices/lib/driver2/inspect.h"
#include "src/devices/lib/driver2/namespace.h"
#include "src/devices/lib/driver2/record_cpp.h"
#include "src/devices/lib/driver2/structured_logger.h"

namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia_io;

namespace {

class PackagedDriver {
 public:
  PackagedDriver(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf::Node> node,
                 driver::Namespace ns, driver::Logger logger)
      : outgoing_(component::OutgoingDirectory::Create(dispatcher)),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)) {}

  static constexpr const char* Name() { return "packaged"; }

  static zx::status<std::unique_ptr<PackagedDriver>> Start(fdf::wire::DriverStartArgs& start_args,
                                                           async_dispatcher_t* dispatcher,
                                                           fidl::WireSharedClient<fdf::Node> node,
                                                           driver::Namespace ns,
                                                           driver::Logger logger) {
    auto driver = std::make_unique<PackagedDriver>(dispatcher, std::move(node), std::move(ns),
                                                   std::move(logger));
    auto result = driver->Run(dispatcher, std::move(start_args.outgoing_dir()));
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx::status<> Run(async_dispatcher* dispatcher, fidl::ServerEnd<fio::Directory> outgoing_dir) {
    auto exposed_inspector = driver::ExposedInspector::Create(dispatcher, inspector_, outgoing_);
    if (exposed_inspector.is_error()) {
      FDF_SLOG(ERROR, "Failed to expose inspector",
               KV("error_string", exposed_inspector.status_string()));
      return exposed_inspector.take_error();
    }

    exposed_inspector_ = std::move(exposed_inspector.value());
    auto& root = inspector_.GetRoot();
    root.CreateString("hello", "world", &inspector_);

    FDF_SLOG(DEBUG, "Debug world");
    FDF_SLOG(INFO, "Hello world", KV("The answer is", 42));
    return outgoing_.Serve(std::move(outgoing_dir));
  }

  component::OutgoingDirectory outgoing_;
  fidl::WireSharedClient<fdf::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;
  inspect::Inspector inspector_;
  std::optional<driver::ExposedInspector> exposed_inspector_ = std::nullopt;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(PackagedDriver);
