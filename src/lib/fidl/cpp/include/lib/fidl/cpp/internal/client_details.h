// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_CLIENT_DETAILS_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_CLIENT_DETAILS_H_

#include <lib/fidl/cpp/unified_messaging.h>
#include <lib/fidl/llcpp/internal/client_details.h>

namespace fidl {
namespace internal {

template <typename Protocol>
AnyIncomingEventDispatcher MakeAnyEventDispatcher(
    fidl::AsyncEventHandler<Protocol>* event_handler) {
  AnyIncomingEventDispatcher event_dispatcher;
  event_dispatcher.emplace<fidl::internal::NaturalEventDispatcher<Protocol>>(event_handler);
  return event_dispatcher;
}

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_CLIENT_DETAILS_H_
