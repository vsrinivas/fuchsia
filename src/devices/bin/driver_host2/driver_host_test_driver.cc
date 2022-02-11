// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driverhost.test/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/epitaph.h>
#include <lib/service/llcpp/outgoing_directory.h>
#include <lib/service/llcpp/service.h>

#include "src/devices/lib/driver2/record.h"
#include "src/devices/lib/driver2/start_args.h"

namespace fdf = fuchsia_driver_framework;
namespace ftest = fuchsia_driverhost_test;

class TestDriver {
 public:
  explicit TestDriver(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher), outgoing_(dispatcher) {}

  zx::status<> Init(fdf::wire::DriverStartArgs* start_args) {
    auto error = driver::SymbolValue<zx_status_t*>(start_args->symbols(), "error");
    if (error.is_ok()) {
      return zx::error(**error);
    }

    // Call the "func" driver symbol.
    auto func = driver::SymbolValue<void (*)()>(start_args->symbols(), "func");
    if (func.is_ok()) {
      (*func)();
    }

    // Set the "dispatcher" driver symbol.
    auto dispatcher =
        driver::SymbolValue<async_dispatcher_t**>(start_args->symbols(), "dispatcher");
    if (dispatcher.is_ok()) {
      **dispatcher = dispatcher_;
    }

    // Connect to the incoming service.
    auto svc_dir = driver::NsValue(start_args->ns(), "/svc");
    if (svc_dir.is_error()) {
      return svc_dir.take_error();
    }
    auto client_end = service::ConnectAt<ftest::Incoming>(svc_dir.value());
    if (!client_end.is_ok()) {
      return client_end.take_error();
    }

    // Setup the outgoing service.
    zx_status_t status = outgoing_.svc_dir()->AddEntry(
        fidl::DiscoverableProtocolName<ftest::Outgoing>,
        fbl::MakeRefCounted<fs::Service>([](fidl::ServerEnd<ftest::Outgoing> request) {
          return fidl_epitaph_write(request.channel().get(), ZX_ERR_STOP);
        }));
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return outgoing_.Serve(std::move(start_args->outgoing_dir()));
  }

 private:
  async_dispatcher_t* dispatcher_;
  service::OutgoingDirectory outgoing_;
};

zx_status_t test_driver_start(fidl_incoming_msg_t* msg, async_dispatcher_t* dispatcher,
                              void** driver) {
  // TODO(fxbug.dev/45252): Use FIDL at rest.
  fidl::unstable::DecodedMessage<fdf::wire::DriverStartArgs> decoded(
      fidl::internal::WireFormatVersion::kV1, msg);
  if (!decoded.ok()) {
    return decoded.status();
  }

  auto test_driver = std::make_unique<TestDriver>(dispatcher);
  auto init = test_driver->Init(decoded.PrimaryObject());
  if (init.is_error()) {
    return init.error_value();
  }

  *driver = test_driver.release();
  return ZX_OK;
}

zx_status_t test_driver_stop(void* driver) {
  delete static_cast<TestDriver*>(driver);
  return ZX_OK;
}

FUCHSIA_DRIVER_RECORD_V1(.start = test_driver_start, .stop = test_driver_stop);
