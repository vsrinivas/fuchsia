// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.devfs.test/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>
#include <lib/service/llcpp/outgoing_directory.h>

#include "src/devices/lib/driver2/devfs_exporter.h"
#include "src/devices/lib/driver2/logger.h"
#include "src/devices/lib/driver2/promise.h"
#include "src/devices/lib/driver2/record.h"

namespace fcd = fuchsia_component_decl;
namespace fdf = fuchsia_driver_framework;
namespace ft = fuchsia_devfs_test;

using fpromise::error;
using fpromise::ok;
using fpromise::promise;
using fpromise::result;

namespace {

class RootDriver : public fidl::WireServer<ft::Device> {
 public:
  explicit RootDriver(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher), executor_(dispatcher), outgoing_(dispatcher) {}

  zx::status<> Start(fdf::wire::DriverStartArgs* start_args) {
    // Bind the node.
    node_.Bind(std::move(start_args->node()), dispatcher_);

    // Create the namespace.
    auto ns = driver::Namespace::Create(start_args->ns());
    if (ns.is_error()) {
      return ns.take_error();
    }
    ns_ = std::move(*ns);

    // Create the logger.
    auto logger = driver::Logger::Create(ns_, dispatcher_, "root");
    if (logger.is_error()) {
      return logger.take_error();
    }
    logger_ = std::move(*logger);

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
    auto serve = outgoing_.Serve(std::move(start_args->outgoing_dir()));
    if (serve.is_error()) {
      return serve.take_error();
    }

    // Create the devfs exporter.
    auto exporter =
        driver::DevfsExporter::Create(ns_, dispatcher_, outgoing_.vfs(), outgoing_.svc_dir());
    if (exporter.is_error()) {
      return exporter.take_error();
    }
    exporter_ = std::move(*exporter);

    // Export "root-device" to devfs.
    auto export_protocol = exporter_.Export<ft::Device>("root-device")
                               .or_else(fit::bind_member(this, &RootDriver::UnbindNode))
                               .wrap_with(scope_);
    executor_.schedule_task(std::move(export_protocol));
    return zx::ok();
  }

 private:
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

zx_status_t DriverStart(fidl_incoming_msg_t* msg, async_dispatcher_t* dispatcher, void** driver) {
  fidl::DecodedMessage<fdf::wire::DriverStartArgs> decoded(msg);
  if (!decoded.ok()) {
    return decoded.status();
  }

  auto root_driver = std::make_unique<RootDriver>(dispatcher);
  auto start = root_driver->Start(decoded.PrimaryObject());
  if (start.is_error()) {
    return start.error_value();
  }

  *driver = root_driver.release();
  return ZX_OK;
}

zx_status_t DriverStop(void* driver) {
  delete static_cast<RootDriver*>(driver);
  return ZX_OK;
}

}  // namespace

FUCHSIA_DRIVER_RECORD_V1(.start = DriverStart, .stop = DriverStop);
