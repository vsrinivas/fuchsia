// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/record_cpp.h>
#include <lib/driver2/structured_logger.h>
#include <lib/inspect/component/cpp/component.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <optional>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

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
                                                           fdf::UnownedDispatcher dispatcher,
                                                           fidl::WireSharedClient<fdf::Node> node,
                                                           driver::Namespace ns,
                                                           driver::Logger logger) {
    auto driver = std::make_unique<PackagedDriver>(dispatcher->async_dispatcher(), std::move(node),
                                                   std::move(ns), std::move(logger));
    auto result = driver->Run(dispatcher->async_dispatcher(), std::move(start_args.outgoing_dir()));
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx::status<> Run(async_dispatcher* dispatcher, fidl::ServerEnd<fio::Directory> outgoing_dir) {
    exposed_inspector_.emplace(inspect::ComponentInspector(outgoing_, dispatcher));
    auto& root = exposed_inspector_->root();
    root.RecordString("hello", "world");

    FDF_SLOG(DEBUG, "Debug world");
    FDF_SLOG(INFO, "Hello world", KV("The answer is", 42));
    return outgoing_.Serve(std::move(outgoing_dir));
  }

  component::OutgoingDirectory outgoing_;
  fidl::WireSharedClient<fdf::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;
  std::optional<inspect::ComponentInspector> exposed_inspector_ = std::nullopt;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(PackagedDriver);
