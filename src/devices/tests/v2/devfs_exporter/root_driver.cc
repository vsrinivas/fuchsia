// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.devfs.test/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/devfs_exporter.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/promise.h>
#include <lib/driver2/record_cpp.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>
#include <lib/service/llcpp/outgoing_directory.h>

namespace fcd = fuchsia_component_decl;
namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia_io;
namespace ft = fuchsia_devfs_test;

using fpromise::error;
using fpromise::ok;
using fpromise::promise;
using fpromise::result;

namespace {

class RootDriver : public fidl::WireServer<ft::Device> {
 public:
  RootDriver(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf::Node> node,
             driver::Namespace ns, driver::Logger logger)
      : dispatcher_(dispatcher),
        executor_(dispatcher),
        outgoing_(dispatcher),
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
    // Setup the outgoing directory.
    auto service = [this](fidl::ServerEnd<ft::Device> server_end) {
      fidl::BindServer(dispatcher_, std::move(server_end), this);
      return ZX_OK;
    };
    zx_status_t status = outgoing_.svc_dir()->AddEntry(fidl::DiscoverableProtocolName<ft::Device>,
                                                       fbl::MakeRefCounted<fs::Service>(service));
    if (status != ZX_OK) {
      return zx::error(status);
    }
    auto serve = outgoing_.Serve(std::move(outgoing_dir));
    if (serve.is_error()) {
      return serve.take_error();
    }

    // Create the devfs exporter.
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      return zx::error(endpoints.status_value());
    }
    status = outgoing_.vfs().Serve(outgoing_.svc_dir(), endpoints->server.TakeChannel(),
                                   fs::VnodeConnectionOptions::ReadWrite());
    if (status != ZX_OK) {
      return zx::error(status);
    }
    auto exporter = driver::DevfsExporter::Create(
        ns_, dispatcher_, fidl::WireSharedClient(std::move(endpoints->client), dispatcher_));
    if (exporter.is_error()) {
      return exporter.take_error();
    }
    exporter_ = std::move(*exporter);

    // Export "root-device" to devfs.
    auto task = exporter_.Export<ft::Device>("root-device")
                    .or_else(fit::bind_member(this, &RootDriver::UnbindNode))
                    .wrap_with(scope_);
    executor_.schedule_task(std::move(task));
    return zx::ok();
  }

  result<> UnbindNode(const zx_status_t& status) {
    FDF_LOG(ERROR, "Failed to start root driver: %s", zx_status_get_string(status));
    node_.AsyncTeardown();
    return ok();
  }

  // fidl::WireServer<ft::Device>
  void Ping(PingRequestView request, PingCompleter::Sync& completer) override { completer.Reply(); }

  async_dispatcher_t* const dispatcher_;
  async::Executor executor_;
  service::OutgoingDirectory outgoing_;

  fidl::WireSharedClient<fdf::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;
  driver::DevfsExporter exporter_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(RootDriver);
