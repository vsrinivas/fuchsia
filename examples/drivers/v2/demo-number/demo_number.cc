// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.compat/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.hardware.demo/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/scope.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>
#include <zircon/errors.h>

#include "src/devices/lib/driver2/devfs_exporter.h"
#include "src/devices/lib/driver2/inspect.h"
#include "src/devices/lib/driver2/namespace.h"
#include "src/devices/lib/driver2/record_cpp.h"
#include "src/devices/lib/driver2/start_args.h"
#include "src/devices/lib/driver2/structured_logger.h"

namespace fdf2 = fuchsia_driver_framework;
namespace fio = fuchsia_io;

namespace {

zx::status<fidl::ClientEnd<fuchsia_driver_compat::Device>> ConnectToParentDevice(
    const driver::Namespace* ns, std::string_view name) {
  auto result = ns->OpenService<fuchsia_driver_compat::Service>(name);
  if (result.is_error()) {
    return result.take_error();
  }
  return result.value().connect_device();
}

zx::status<driver::DevfsExporter> ConnectToDevfsExporter(async_dispatcher_t* dispatcher,
                                                         const driver::Namespace* ns,
                                                         component::OutgoingDirectory* outgoing) {
  // Connect to DevfsExporter.
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  // Serve a connection to outgoing.
  auto status = outgoing->Serve(std::move(endpoints->server));
  if (status.is_error()) {
    return status.take_error();
  }

  return driver::DevfsExporter::Create(
      *ns, dispatcher, fidl::WireSharedClient(std::move(endpoints->client), dispatcher));
}

class DemoNumber : public fidl::WireServer<fuchsia_hardware_demo::Demo> {
 public:
  DemoNumber(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf2::Node> node,
             driver::Namespace ns, component::OutgoingDirectory outgoing, driver::Logger logger)
      : dispatcher_(dispatcher),
        outgoing_(std::move(outgoing)),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)),
        executor_(dispatcher) {}

  static constexpr const char* Name() { return "demo_number"; }

  static zx::status<std::unique_ptr<DemoNumber>> Start(fdf2::wire::DriverStartArgs& start_args,
                                                       async_dispatcher_t* dispatcher,
                                                       fidl::WireSharedClient<fdf2::Node> node,
                                                       driver::Namespace ns,
                                                       driver::Logger logger) {
    auto outgoing = component::OutgoingDirectory::Create(dispatcher);
    auto driver = std::make_unique<DemoNumber>(dispatcher, std::move(node), std::move(ns),
                                               std::move(outgoing), std::move(logger));

    auto result = driver->Run(std::move(start_args.outgoing_dir()));
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx::status<> Run(fidl::ServerEnd<fio::Directory> outgoing_dir) {
    // Connect to DevfsExporter.
    auto exporter = ConnectToDevfsExporter(dispatcher_, &ns_, &outgoing_);
    if (exporter.is_error()) {
      return exporter.take_error();
    }
    exporter_ = std::move(*exporter);

    auto parent = ConnectToParentDevice(&ns_, "default");
    if (parent.status_value() != ZX_OK) {
      return parent.take_error();
    }

    auto result = fidl::WireCall(*parent)->GetTopologicalPath();
    if (!result.ok()) {
      return zx::error(result.status());
    }

    std::string path(result->path.data(), result->path.size());

    auto status = outgoing_.AddProtocol<fuchsia_hardware_demo::Demo>(this);
    if (status.status_value() != ZX_OK) {
      return status;
    }

    path.append("/");
    path.append(Name());

    FDF_LOG(INFO, "Exporting device to: %s", path.data());
    auto task =
        exporter_.Export("svc/fuchsia.hardware.demo.Demo", path, 0)
            .or_else([this](zx_status_t& result) {
              FDF_LOG(WARNING, "Device setup failed with: %s", zx_status_get_string(result));
            });
    executor_.schedule_task(std::move(task));

    return outgoing_.Serve(std::move(outgoing_dir));
  }

  void GetNumber(GetNumberRequestView request, GetNumberCompleter::Sync& completer) override {
    completer.Reply(current_number);
    current_number += 1;
  }

  async_dispatcher_t* dispatcher_;
  component::OutgoingDirectory outgoing_;

  fidl::WireSharedClient<fdf2::Node> node_;

  driver::Namespace ns_;
  driver::Logger logger_;
  async::Executor executor_;
  driver::DevfsExporter exporter_;

  std::string parent_topo_path_;
  uint32_t current_number = 0;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(DemoNumber);
