// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_RECORD_CPP_H_
#define LIB_DRIVER2_RECORD_CPP_H_

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/record.h>
#include <lib/fdf/cpp/dispatcher.h>

namespace driver::internal {

// Starts a driver `T` from `msg` and `dispatcher`, and stores the result in
// `driver`.
//
// This function expects `T` to contain:
// 1. A static function `T::Name` that returns the name to use for the logger.
// 2. A member function `T::Start` with the signature:
//    zx::result<std::unique_ptr<T>> Start(
//      fuchsia_driver_framework::wire::DriverStartArgs& start_args,
//      fdf::UnownedDispatcher dispatcher,
//      fidl::WireSharedClient<fuchsia_driver_framework::Node> node,
//      driver::Namespace ns,
//      driver::Logger logger)
template <typename T>
zx_status_t Start(EncodedDriverStartArgs encoded_start_args, fdf_dispatcher_t* dispatcher,
                  void** driver) {
  // Decode the incoming `msg`.
  auto wire_format_metadata =
      fidl::WireFormatMetadata::FromOpaque(encoded_start_args.wire_format_metadata);
  fit::result start_args = fidl::InplaceDecode<fuchsia_driver_framework::wire::DriverStartArgs>(
      fidl::EncodedMessage::FromEncodedCMessage(encoded_start_args.msg), wire_format_metadata);
  if (!start_args.is_ok()) {
    return start_args.error_value().status();
  }

  // Bind the node.
  fidl::WireSharedClient<fuchsia_driver_framework::Node> node(
      std::move(start_args->node()), fdf_dispatcher_get_async_dispatcher(dispatcher));

  // Create the namespace.
  auto ns = driver::Namespace::Create(start_args->ns());
  if (ns.is_error()) {
    return ns.status_value();
  }

  // Create the logger.
  auto logger =
      driver::Logger::Create(*ns, fdf_dispatcher_get_async_dispatcher(dispatcher), T::Name());
  if (logger.is_error()) {
    return logger.status_value();
  }

  // Create the driver.
  auto self = T::Start(**start_args, fdf::UnownedDispatcher(dispatcher), std::move(node),
                       std::move(*ns), std::move(*logger));
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

#endif  // LIB_DRIVER2_RECORD_CPP_H_
