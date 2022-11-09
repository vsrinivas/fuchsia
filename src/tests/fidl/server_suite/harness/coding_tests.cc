// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tests/fidl/channel_util/bytes.h"
#include "src/tests/fidl/server_suite/harness/harness.h"
#include "src/tests/fidl/server_suite/harness/ordinals.h"

using namespace channel_util;

namespace server_suite {

CLOSED_SERVER_TEST(BadPayloadEncoding) {
  Bytes bytes_in = {
      // clang-format off
      header(123, kOrdinalTwoWayResult, fidl::MessageDynamicFlags::kStrictMethod),
      // Ordinal 3 is unknown in the FIDL schema, but the union is strict.
      union_ordinal(3), out_of_line_envelope(0, 0),
      // clang-format on
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
}

CLOSED_SERVER_TEST(V1TwoWayNoPayload) {
  Bytes bytes_in = {
      as_bytes(fidl_message_header_t{
          .txid = 123,
          // at rest flags, without the V2 indicator
          .at_rest_flags = {0, 0},
          .dynamic_flags = FIDL_MESSAGE_HEADER_DYNAMIC_FLAGS_STRICT_METHOD,
          .magic_number = kFidlWireFormatMagicNumberInitial,
          .ordinal = kOrdinalTwoWayNoPayload,
      }),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
}

CLOSED_SERVER_TEST(V1TwoWayStructPayload) {
  Bytes bytes_in = {
      // header
      as_bytes(fidl_message_header_t{
          .txid = 123,
          // at rest flags, without the V2 indicator
          .at_rest_flags = {0, 0},
          .dynamic_flags = FIDL_MESSAGE_HEADER_DYNAMIC_FLAGS_STRICT_METHOD,
          .magic_number = kFidlWireFormatMagicNumberInitial,
          .ordinal = kOrdinalTwoWayStructPayload,
      }),

      // body, then padded to 8 bytes
      i8(0),
      padding(7),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
}

}  // namespace server_suite
