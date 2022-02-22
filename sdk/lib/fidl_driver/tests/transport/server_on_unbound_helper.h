// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_TESTS_TRANSPORT_SERVER_ON_UNBOUND_HELPER_H_
#define LIB_FIDL_DRIVER_TESTS_TRANSPORT_SERVER_ON_UNBOUND_HELPER_H_

#include <lib/fidl/llcpp/server.h>
#include <lib/fidl_driver/cpp/transport.h>
#include <lib/fidl_driver/cpp/wire_messaging_declarations.h>

#include <zxtest/zxtest.h>

namespace fidl_driver_testing {

// Returns an |OnUnboundFn| that injects a test error when the server
// unbinds from the channel due to an error.
template <typename FidlProtocol>
auto FailTestOnServerError() {
  return [](fdf::WireServer<FidlProtocol>*, fidl::UnbindInfo info,
            fdf::ServerEnd<FidlProtocol> server_end) {
    if (info.is_dispatcher_shutdown())
      return;
    if (info.is_user_initiated())
      return;
    if (info.is_peer_closed())
      return;
    ADD_FAILURE("Server error: %s", info.FormatDescription().c_str());
  };
}

}  // namespace fidl_driver_testing

#endif  // LIB_FIDL_DRIVER_TESTS_TRANSPORT_SERVER_ON_UNBOUND_HELPER_H_
