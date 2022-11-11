// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tests/fidl/server_suite/harness/harness.h"
#include "src/tests/fidl/server_suite/harness/ordinals.h"

using namespace channel_util;

namespace server_suite {

constexpr uint32_t maxVecBytesInMsg =
    ZX_CHANNEL_MAX_MSG_BYTES - sizeof(fidl_message_header_t) - sizeof(fidl_vector_t);
constexpr uint32_t maxVecHandlesInMsg = ZX_CHANNEL_MAX_MSG_HANDLES;

CLOSED_SERVER_TEST(RequestMatchesByteLimit) {
  constexpr uint32_t n = maxVecBytesInMsg;

  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalByteVectorSize, fidl::MessageDynamicFlags::kStrictMethod),
      vector_header(n),
      repeat(0).times(n),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalByteVectorSize, fidl::MessageDynamicFlags::kStrictMethod),
      u32(n),
      padding(4),
  };
  ASSERT_OK(client_end().write(bytes_out));
}

CLOSED_SERVER_TEST(RequestMatchesHandleLimit) {
  constexpr uint32_t n = maxVecHandlesInMsg;

  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalHandleVectorSize, fidl::MessageDynamicFlags::kStrictMethod),
      vector_header(n),
      repeat(0xff).times(n * sizeof(zx_handle_t)),
  };
  HandleDispositions handle_dispositions_in;
  for (uint32_t i = 0; i < n; i++) {
    zx::event ev;
    ASSERT_OK(zx::event::create(0, &ev));
    handle_dispositions_in.push_back(zx_handle_disposition_t{
        .operation = ZX_HANDLE_OP_MOVE,
        .handle = ev.release(),
        .type = ZX_OBJ_TYPE_EVENT,
        .rights = ZX_DEFAULT_EVENT_RIGHTS,
    });
  }
  ASSERT_OK(client_end().write(bytes_in, handle_dispositions_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalHandleVectorSize, fidl::MessageDynamicFlags::kStrictMethod),
      u32(n),
      padding(4),
  };
  ASSERT_OK(client_end().write(bytes_out));
}

CLOSED_SERVER_TEST(ResponseMatchesByteLimit) {
  constexpr uint32_t n = maxVecBytesInMsg;

  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalCreateNByteVector, fidl::MessageDynamicFlags::kStrictMethod),
      u32(n),
      padding(4),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalCreateNByteVector, fidl::MessageDynamicFlags::kStrictMethod),
      vector_header(n),
      repeat(0).times(n),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

CLOSED_SERVER_TEST(ResponseExceedsByteLimit) {
  constexpr uint32_t n = maxVecBytesInMsg + 1;

  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalCreateNByteVector, fidl::MessageDynamicFlags::kStrictMethod),
      u32(n),
      padding(4),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
}

CLOSED_SERVER_TEST(ResponseMatchesHandleLimit) {
  constexpr uint32_t n = maxVecHandlesInMsg;

  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalCreateNHandleVector, fidl::MessageDynamicFlags::kStrictMethod),
      u32(n),
      padding(4),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalCreateNHandleVector, fidl::MessageDynamicFlags::kStrictMethod),
      vector_header(n),
      repeat(0xff).times(n * sizeof(zx_handle_t)),
  };
  HandleInfos handle_infos_out;
  for (uint32_t i = 0; i < n; i++) {
    handle_infos_out.push_back(zx_handle_info_t{
        .type = ZX_OBJ_TYPE_EVENT,
        .rights = ZX_DEFAULT_EVENT_RIGHTS,
    });
  }
  ASSERT_OK(client_end().read_and_check(bytes_out, handle_infos_out));
}

CLOSED_SERVER_TEST(ResponseExceedsHandleLimit) {
  constexpr uint32_t n = maxVecHandlesInMsg + 1;

  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalCreateNHandleVector, fidl::MessageDynamicFlags::kStrictMethod),
      u32(n),
      padding(4),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
}

}  // namespace server_suite
