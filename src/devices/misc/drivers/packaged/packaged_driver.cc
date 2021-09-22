// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/svc/outgoing.h>

#include "src/devices/lib/driver2/inspect.h"
#include "src/devices/lib/driver2/logger.h"
#include "src/devices/lib/driver2/namespace.h"
#include "src/devices/lib/driver2/record.h"

namespace {

namespace fdf = fuchsia_driver_framework;

class PackagedDriver {
 public:
  explicit PackagedDriver(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher), outgoing_(dispatcher) {}

  zx::status<> Init(fdf::wire::DriverStartArgs* start_args) {
    node_.Bind(std::move(start_args->node()), dispatcher_);

    auto ns = driver::Namespace::Create(start_args->ns());
    if (ns.is_error()) {
      return ns.take_error();
    }
    ns_ = std::move(ns.value());

    auto logger = driver::Logger::Create(ns_, dispatcher_, "packaged_driver");
    if (logger.is_error()) {
      return logger.take_error();
    }
    logger_ = std::move(logger.value());

    auto inspect = driver::ExposeInspector(inspector_, outgoing_.root_dir());
    if (inspect.is_error()) {
      FDF_LOG(ERROR, "Failed to expose inspector: %s", inspect.status_string());
      return inspect.take_error();
    }
    inspect_vmo_ = std::move(inspect.value());

    FDF_LOG(INFO, "Hello world");
    auto& root = inspector_.GetRoot();
    root.CreateString("hello", "world", &inspector_);
    zx_status_t status = outgoing_.Serve(std::move(start_args->outgoing_dir()));
    return zx::make_status(status);
  }

 private:
  async_dispatcher_t* dispatcher_;
  svc::Outgoing outgoing_;
  fidl::WireSharedClient<fdf::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;
  inspect::Inspector inspector_;
  zx::vmo inspect_vmo_;
};

zx_status_t PackagedDriverStart(fidl_incoming_msg_t* msg, async_dispatcher_t* dispatcher,
                                void** driver) {
  fdf::wire::DriverStartArgs::DecodedMessage decoded(fidl::internal::kLLCPPEncodedWireFormatVersion,
                                                     msg);
  if (!decoded.ok()) {
    return decoded.status();
  }

  auto packaged_driver = std::make_unique<PackagedDriver>(dispatcher);
  auto init = packaged_driver->Init(decoded.PrimaryObject());
  if (init.is_error()) {
    return init.error_value();
  }

  *driver = packaged_driver.release();
  return ZX_OK;
}

zx_status_t PackagedDriverStop(void* driver) {
  delete static_cast<PackagedDriver*>(driver);
  return ZX_OK;
}

}  // namespace

FUCHSIA_DRIVER_RECORD_V1(.start = PackagedDriverStart, .stop = PackagedDriverStop);
