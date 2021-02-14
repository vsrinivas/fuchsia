// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/driverhost/test/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/epitaph.h>
#include <lib/svc/outgoing.h>

#include "src/devices/lib/driver2/record.h"
#include "src/devices/lib/driver2/start_args.h"

namespace fdf = llcpp::fuchsia::driver::framework;
namespace ftest = llcpp::fuchsia::driverhost::test;

class TestDriver {
 public:
  explicit TestDriver(async_dispatcher_t* dispatcher) : outgoing_(dispatcher) {}

  zx::status<> Init(fdf::DriverStartArgs* start_args) {
    // Call the "func" driver symbol.
    auto func = start_args::SymbolValue<void (*)()>(start_args->symbols(), "func");
    if (func.is_ok()) {
      func.value()();
    }

    // Connect to the incoming service.
    auto svc_dir = start_args::NsValue(start_args->ns(), "/svc");
    if (svc_dir.is_error()) {
      return svc_dir.take_error();
    }
    zx::channel client_end, server_end;
    zx_status_t status = zx::channel::create(0, &client_end, &server_end);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    status = fdio_service_connect_at(svc_dir.value().channel(), ftest::Incoming::Name,
                                     server_end.release());
    if (status != ZX_OK) {
      return zx::error(status);
    }

    // Setup the outgoing service.
    status = outgoing_.svc_dir()->AddEntry(
        ftest::Outgoing::Name, fbl::MakeRefCounted<fs::Service>([](zx::channel request) {
          return fidl_epitaph_write(request.get(), ZX_ERR_STOP);
        }));
    if (status != ZX_OK) {
      return zx::error(status);
    }
    status = outgoing_.Serve(std::move(start_args->outgoing_dir()));
    return zx::make_status(status);
  }

 private:
  svc::Outgoing outgoing_;
};

zx_status_t test_driver_start(fidl_incoming_msg_t* msg, async_dispatcher_t* dispatcher,
                              void** driver) {
  fdf::DriverStartArgs::DecodedMessage decoded(msg);
  if (!decoded.ok()) {
    return decoded.status();
  }

  auto test_driver = new TestDriver(dispatcher);
  *driver = test_driver;
  return test_driver->Init(decoded.PrimaryObject()).status_value();
}

zx_status_t test_driver_stop(void* driver) {
  delete static_cast<TestDriver*>(driver);
  return ZX_OK;
}

FUCHSIA_DRIVER_RECORD_V1(.start = test_driver_start, .stop = test_driver_stop);
