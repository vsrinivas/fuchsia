// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tests/fidl/server_suite/harness/harness.h"
#include "src/tests/fidl/server_suite/harness/ordinals.h"

using namespace channel_util;

namespace server_suite {

// The server sends epitaphs to clients.
CLOSED_SERVER_TEST(ServerSendsEpitaph) {
  constexpr zx_status_t sent_status = 456;

  Bytes bytes_out = {
      header(kOneWayTxid, kOrdinalCloseWithEpitaph, fidl::MessageDynamicFlags::kStrictMethod),
      i32(sent_status),
      padding(4),
  };
  ASSERT_OK(client_end().write(bytes_out));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalEpitaph, fidl::MessageDynamicFlags::kStrictMethod),
      i32(sent_status),
      padding(4),
  };
  ASSERT_OK(client_end().read_and_check(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
}

// It is not permissible to send epitaphs to servers.
CLOSED_SERVER_TEST(ServerReceivesEpitaphInvalid) {
  Bytes bytes_out = {
      header(kOneWayTxid, kOrdinalEpitaph, fidl::MessageDynamicFlags::kStrictMethod),
      i32(456),
      padding(4),
  };
  ASSERT_OK(client_end().write(bytes_out));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
}

}  // namespace server_suite
