// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/driverhost/test/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/epitaph.h>
#include <lib/service/llcpp/service.h>
#include <lib/svc/outgoing.h>

#include "src/devices/lib/driver2/record.h"
#include "src/devices/lib/driver2/start_args.h"

namespace fdf = fuchsia_driver_framework;
namespace ftest = fuchsia_driverhost_test;

class TestDriver {
 public:
  explicit TestDriver(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher), outgoing_(dispatcher) {}

  zx::status<> Init(fdf::wire::DriverStartArgs* start_args) {
    // Call the "func" driver symbol.
    auto func =
        start_args::SymbolValue<void (*)(async_dispatcher_t*)>(start_args->symbols(), "func");
    if (func.is_ok()) {
      func.value()(dispatcher_);
    }

    // Connect to the incoming service.
    auto svc_dir = start_args::NsValue(start_args->ns(), "/svc");
    if (svc_dir.is_error()) {
      return svc_dir.take_error();
    }
    auto client_end = service::ConnectAt<ftest::Incoming>(svc_dir.value());
    if (!client_end.is_ok()) {
      return client_end.take_error();
    }

    // Setup the outgoing service.
    zx_status_t status =
        outgoing_.svc_dir()->AddEntry(fidl::DiscoverableProtocolName<ftest::Outgoing>,
                                      fbl::MakeRefCounted<fs::Service>([](zx::channel request) {
                                        return fidl_epitaph_write(request.get(), ZX_ERR_STOP);
                                      }));
    if (status != ZX_OK) {
      return zx::error(status);
    }
    status = outgoing_.Serve(std::move(start_args->outgoing_dir()));
    return zx::make_status(status);
  }

 private:
  async_dispatcher_t* dispatcher_;
  svc::Outgoing outgoing_;
};

zx_status_t test_driver_start(fidl_incoming_msg_t* msg, async_dispatcher_t* dispatcher,
                              void** driver) {
  fdf::wire::DriverStartArgs::DecodedMessage decoded(msg);
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
