// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_COMPONENT_CPP_HANDLERS_H_
#define LIB_DRIVER_COMPONENT_CPP_HANDLERS_H_

#include <lib/fidl/cpp/wire/service_handler.h>
#include <lib/fidl_driver/cpp/transport.h>

namespace driver {

// Callback invoked when a request is made to a FIDL protocol server end.
using AnyHandler = fit::function<void(typename fidl::internal::DriverTransport::OwnedType)>;

// Callback invoked when a request is made to a protocol server end.
template <typename Protocol>
using TypedHandler = fit::function<void(fidl::internal::ServerEndType<Protocol> request)>;

using ServiceInstanceHandler = fidl::ServiceInstanceHandler<fidl::internal::DriverTransport>;

}  // namespace driver

#endif  // LIB_DRIVER_COMPONENT_CPP_HANDLERS_H_
