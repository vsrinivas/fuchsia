// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/fidl.h>

#include "fidl/fidl.clientsuite/cpp/markers.h"
#include "src/tests/fidl/channel_util/bytes.h"
#include "src/tests/fidl/client_suite/harness/harness.h"
#include "src/tests/fidl/client_suite/harness/ordinals.h"

using namespace channel_util;

namespace client_suite {

CLIENT_TEST(V1TwoWayNoPayload) {
  runner()->CallTwoWayNoPayload({{.target = TakeClosedClient()}}).ThenExactlyOnce([&](auto result) {
    MarkCallbackRun();
    ASSERT_TRUE(result.is_ok()) << result.error_value();
    ASSERT_TRUE(result.value().fidl_error().has_value());
    ASSERT_EQ(result.value().fidl_error().value(), fidl_clientsuite::FidlErrorKind::kDecodingError);
  });

  ASSERT_OK(server_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(0, kOrdinalTwoWayNoPayload, fidl::MessageDynamicFlags::kStrictMethod),
  };
  zx_txid_t txid;
  ASSERT_OK(server_end().read_and_check_unknown_txid(&txid, bytes_out));
  ASSERT_NE(0u, txid);

  Bytes bytes_in = {
      as_bytes(fidl_message_header_t{
          .txid = txid,
          // at rest flags, without the V2 indicator
          .at_rest_flags = {0, 0},
          .dynamic_flags = FIDL_MESSAGE_HEADER_DYNAMIC_FLAGS_STRICT_METHOD,
          .magic_number = kFidlWireFormatMagicNumberInitial,
          .ordinal = kOrdinalTwoWayNoPayload,
      }),
  };
  ASSERT_OK(server_end().write(bytes_in));

  WAIT_UNTIL_CALLBACK_RUN();
}

CLIENT_TEST(V1TwoWayStructPayload) {
  static const fidl_clientsuite::NonEmptyPayload kPayload{{.some_field = 42}};

  runner()
      ->CallTwoWayStructPayload({{.target = TakeClosedClient()}})
      .ThenExactlyOnce(
          [&](fidl::Result<fidl_clientsuite::Runner::CallTwoWayStructPayload>& result) {
            MarkCallbackRun();
            ASSERT_TRUE(result.is_ok()) << result.error_value();
            ASSERT_TRUE(result.value().fidl_error().has_value());
            ASSERT_EQ(result.value().fidl_error().value(),
                      fidl_clientsuite::FidlErrorKind::kDecodingError);
          });

  ASSERT_OK(server_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(0, kOrdinalTwoWayStructPayload, fidl::MessageDynamicFlags::kStrictMethod),
  };
  zx_txid_t txid;
  ASSERT_OK(server_end().read_and_check_unknown_txid(&txid, bytes_out));
  ASSERT_NE(0u, txid);

  Bytes bytes_in = {
      // header
      as_bytes(fidl_message_header_t{
          .txid = txid,
          // at rest flags, without the V2 indicator
          .at_rest_flags = {0, 0},
          .dynamic_flags = FIDL_MESSAGE_HEADER_DYNAMIC_FLAGS_STRICT_METHOD,
          .magic_number = kFidlWireFormatMagicNumberInitial,
          .ordinal = kOrdinalTwoWayStructPayload,
      }),

      // body
      i32(42),
      padding(4),
  };
  ASSERT_OK(server_end().write(bytes_in));

  WAIT_UNTIL_CALLBACK_RUN();
}

}  // namespace client_suite
