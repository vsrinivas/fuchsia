// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_NATURAL_CLIENT_DETAILS_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_NATURAL_CLIENT_DETAILS_H_

#include <lib/fidl_driver/cpp/internal/wire_client_details.h>
#include <lib/fidl_driver/cpp/unified_messaging_declarations.h>

namespace fidl::internal {

template <typename Protocol>
AnyIncomingEventDispatcher MakeAnyEventDispatcher(fdf::AsyncEventHandler<Protocol>* event_handler) {
  AnyIncomingEventDispatcher event_dispatcher;
  event_dispatcher.emplace<fidl::internal::NaturalEventDispatcher<Protocol>>(event_handler);
  return event_dispatcher;
}

}  // namespace fidl::internal

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_NATURAL_CLIENT_DETAILS_H_
