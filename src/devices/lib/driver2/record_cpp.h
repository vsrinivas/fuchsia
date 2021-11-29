// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_RECORD_CPP_H_
#define SRC_DEVICES_LIB_DRIVER2_RECORD_CPP_H_

#include <fidl/fuchsia.driver.framework/cpp/wire.h>

#include "src/devices/lib/driver2/logger.h"
#include "src/devices/lib/driver2/record.h"

namespace driver::internal {

// Starts a driver `T` from `msg` and `dispatcher`, and stores the result in
// `driver`.
//
// This function expects `T` to contain:
// 1. A static function `T::Name` that returns the name to use for the logger.
// 2. A member function `T::Start` with the signature:
//    zx::status<std::unique_ptr<T>> Start(
//      fuchsia_driver_framework::wire::DriverStartArgs& start_args,
//      async_dispatcher_t* dispatcher,
//      fidl::WireSharedClient<fuchsia_driver_framework::Node> node,
//      driver::Namespace ns,
//      driver::Logger logger)
template <typename T>
zx_status_t Start(fidl_incoming_msg_t* msg, async_dispatcher_t* dispatcher, void** driver) {
  // Decode the incoming `msg`.
  fidl::DecodedMessage<fuchsia_driver_framework::wire::DriverStartArgs> decoded(msg);
  if (!decoded.ok()) {
    return decoded.status();
  }
  auto start_args = decoded.PrimaryObject();

  // Bind the node.
  fidl::WireSharedClient<fuchsia_driver_framework::Node> node(std::move(start_args->node()),
                                                              dispatcher);

  // Create the namespace.
  auto ns = driver::Namespace::Create(start_args->ns());
  if (ns.is_error()) {
    return ns.status_value();
  }

  // Create the logger.
  auto logger = driver::Logger::Create(*ns, dispatcher, T::Name());
  if (logger.is_error()) {
    return logger.status_value();
  }

  // Create the driver.
  auto self =
      T::Start(*start_args, dispatcher, std::move(node), std::move(*ns), std::move(*logger));
  if (self.is_error()) {
    return self.status_value();
  }

  // Store `driver` pointer.
  *driver = (*self).release();
  return ZX_OK;
}

// Stops a driver `T` by deleting it.
template <typename T>
zx_status_t Stop(void* driver) {
  delete static_cast<T*>(driver);
  return ZX_OK;
}

}  // namespace driver::internal

#define FUCHSIA_DRIVER_RECORD_CPP_V1(T) \
  FUCHSIA_DRIVER_RECORD_V1(.start = driver::internal::Start<T>, .stop = driver::internal::Stop<T>)

#endif  // SRC_DEVICES_LIB_DRIVER2_RECORD_CPP_H_
