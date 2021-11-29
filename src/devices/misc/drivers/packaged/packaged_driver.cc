// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/service/llcpp/outgoing_directory.h>

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
      : outgoing_(dispatcher),
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
    auto result = driver->Run(std::move(start_args.outgoing_dir()));
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx::status<> Run(fidl::ServerEnd<fio::Directory> outgoing_dir) {
    auto inspect = driver::ExposeInspector(inspector_, outgoing_.root_dir());
    if (inspect.is_error()) {
      FDF_SLOG(ERROR, "Failed to expose inspector", KV("error_string", inspect.status_string()));
      return inspect.take_error();
    }
    inspect_vmo_ = std::move(inspect.value());
    auto& root = inspector_.GetRoot();
    root.CreateString("hello", "world", &inspector_);

    FDF_SLOG(DEBUG, "Debug world");
    FDF_SLOG(INFO, "Hello world", KV("The answer is", 42));
    return outgoing_.Serve(std::move(outgoing_dir));
  }

  service::OutgoingDirectory outgoing_;
  fidl::WireSharedClient<fdf::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;
  inspect::Inspector inspector_;
  zx::vmo inspect_vmo_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(PackagedDriver);
