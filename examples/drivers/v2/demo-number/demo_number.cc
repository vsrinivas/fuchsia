// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.fs/cpp/wire.h>
#include <fidl/fuchsia.driver.compat/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.hardware.demo/cpp/wire.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/record_cpp.h>
#include <lib/driver2/start_args.h>
#include <lib/driver2/structured_logger.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <zircon/errors.h>

namespace fdf2 = fuchsia_driver_framework;
namespace fio = fuchsia_io;

namespace {

class DemoNumber : public fidl::WireServer<fuchsia_hardware_demo::Demo> {
 public:
  DemoNumber(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf2::Node> node,
             driver::Namespace ns, component::OutgoingDirectory outgoing, driver::Logger logger)
      : dispatcher_(dispatcher),
        outgoing_(std::move(outgoing)),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)) {}

  static constexpr const char* Name() { return "demo_number"; }

  static zx::status<std::unique_ptr<DemoNumber>> Start(fdf2::wire::DriverStartArgs& start_args,
                                                       fdf::UnownedDispatcher dispatcher,
                                                       fidl::WireSharedClient<fdf2::Node> node,
                                                       driver::Namespace ns,
                                                       driver::Logger logger) {
    auto outgoing = component::OutgoingDirectory::Create(dispatcher->async_dispatcher());
    auto driver =
        std::make_unique<DemoNumber>(dispatcher->async_dispatcher(), std::move(node), std::move(ns),
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
    auto exporter = ns_.Connect<fuchsia_device_fs::Exporter>();
    if (exporter.status_value() != ZX_OK) {
      return exporter.take_error();
    }
    exporter_ = fidl::WireClient(std::move(*exporter), dispatcher_);

    // Connect to parent.
    auto default_parent = ns_.OpenService<fuchsia_driver_compat::Service>("default");
    if (default_parent.is_error()) {
      return default_parent.take_error();
    }
    auto parent = default_parent->connect_device();
    if (parent.status_value() != ZX_OK) {
      return parent.take_error();
    }

    auto result = fidl::WireCall(*parent)->GetTopologicalPath();
    if (!result.ok()) {
      return zx::error(result.status());
    }

    std::string path(result.value().path.data(), result.value().path.size());

    auto status = outgoing_.AddProtocol<fuchsia_hardware_demo::Demo>(this, Name());
    if (status.status_value() != ZX_OK) {
      return status;
    }

    path.append("/");
    path.append(Name());

    FDF_LOG(INFO, "Exporting device to: %s", path.data());

    // Serve a connection to outgoing.
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    {
      auto status = outgoing_.Serve(std::move(endpoints->server));
      if (status.is_error()) {
        return status.take_error();
      }
    }

    // Export our protocol.
    exporter_
        ->Export(std::move(endpoints->client),
                 fidl::StringView::FromExternal(std::string("svc/").append(Name())),
                 fidl::StringView::FromExternal(path), 0)
        .Then([this](fidl::WireUnownedResult<fuchsia_device_fs::Exporter::Export>& result) {
          if (!result.ok()) {
            FDF_LOG(ERROR, "Exporting failed with: %s", result.status_string());
          }
        });

    return outgoing_.Serve(std::move(outgoing_dir));
  }

  void GetNumber(GetNumberCompleter::Sync& completer) override {
    completer.Reply(current_number);
    current_number += 1;
  }

  async_dispatcher_t* dispatcher_;
  component::OutgoingDirectory outgoing_;

  fidl::WireSharedClient<fdf2::Node> node_;

  driver::Namespace ns_;
  driver::Logger logger_;
  fidl::WireClient<fuchsia_device_fs::Exporter> exporter_;

  std::string parent_topo_path_;
  uint32_t current_number = 0;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(DemoNumber);
