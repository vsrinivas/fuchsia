// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tests/fidl/server_suite/harness/harness.h"
#include "src/tests/fidl/server_suite/harness/ordinals.h"

using namespace channel_util;

namespace server_suite {

SERVER_TEST(BadPayloadEncoding) {
  Bytes bytes_in = {
      // clang-format off
      header(123, kOrdinalTwoWayResult, fidl::MessageDynamicFlags::kStrictMethod),
      union_ordinal(3), out_of_line_envelope(0, 0),
      // clang-format on
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
}

}  // namespace server_suite
