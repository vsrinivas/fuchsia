// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_HANDLERS_H_
#define LIB_SYS_COMPONENT_CPP_HANDLERS_H_

#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/fidl/cpp/wire/service_handler.h>

namespace component {

// Callback invoked when a request is made to a FIDL protocol server end.
using AnyHandler = fit::function<void(typename fidl::internal::ChannelTransport::OwnedType)>;

// Callback invoked when a request is made to a protocol server end.
template <typename Protocol>
using TypedHandler = fit::function<void(fidl::internal::ServerEndType<Protocol> request)>;

using ServiceInstanceHandler = fidl::ServiceInstanceHandler<fidl::internal::ChannelTransport>;

}  // namespace component

#endif  // LIB_SYS_COMPONENT_CPP_HANDLERS_H_
