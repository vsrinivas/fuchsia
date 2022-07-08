// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harness.h"
#include "ordinals.h"

// Check that the test runner is set up correctly without doing anything else.
SERVER_TEST(Setup) {}

// Check that a one-way call is received at Target.
SERVER_TEST(OneWayNoPayload) {
  fidl_message_header_t hdr;
  fidl::InitTxnHeader(&hdr, 0, kOrdinalOneWayNoPayload, fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_OK(client_end().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  WAIT_UNTIL([this]() { return reporter().received_one_way_no_payload(); });
}

// Check that the channel is closed when a new one-way request with a non-zero txid is received.
SERVER_TEST(OneWayWithNonZeroTxid) {
  fidl_message_header_t hdr;
  fidl::InitTxnHeader(&hdr, 56 /* txid not 0 */, kOrdinalOneWayNoPayload,
                      fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_OK(client_end().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  ASSERT_OK(client_end().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::sec(5)), nullptr));
}

SERVER_TEST(TwoWayNoPayload) {
  constexpr zx_txid_t kTxid = 123u;

  fidl_message_header_t hdr_out;
  fidl::InitTxnHeader(&hdr_out, kTxid, kOrdinalTwoWayNoPayload,
                      fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_OK(client_end().write(0, &hdr_out, sizeof(fidl_message_header_t), nullptr, 0));

  ASSERT_OK(client_end().wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(zx::sec(5)), nullptr));

  fidl_message_header_t hdr_in;
  uint32_t actual_bytes;
  uint32_t actual_handles;
  ASSERT_OK(
      client_end().read(0, &hdr_in, nullptr, sizeof(hdr_in), 0, &actual_bytes, &actual_handles));
  ASSERT_EQ(sizeof(hdr_in), actual_bytes);
  ASSERT_EQ(0u, actual_handles);

  ASSERT_EQ(kOrdinalTwoWayNoPayload, hdr_in.ordinal);
  ASSERT_EQ(kTxid, hdr_in.txid);
  ASSERT_EQ(FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, hdr_in.at_rest_flags[0]);
  ASSERT_EQ(0, hdr_in.at_rest_flags[1]);
  ASSERT_EQ(0, hdr_in.dynamic_flags);
  ASSERT_EQ(kFidlWireFormatMagicNumberInitial, hdr_in.magic_number);
}

SERVER_TEST(TwoWayNoPayloadWithZeroTxid) {
  fidl_message_header_t hdr_out;
  fidl::InitTxnHeader(&hdr_out, 0, kOrdinalTwoWayNoPayload,
                      fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_OK(client_end().write(0, &hdr_out, sizeof(fidl_message_header_t), nullptr, 0));

  ASSERT_OK(client_end().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::sec(5)), nullptr));
}

// Check that the server closes the channel when unknown ordinals are received.
SERVER_TEST(UnknownOrdinalCausesClose) {
  fidl_message_header_t hdr;
  fidl::InitTxnHeader(&hdr, 0, /* some wrong ordinal */ 8888888lu,
                      fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_OK(client_end().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  ASSERT_OK(client_end().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::sec(5)), nullptr));
}
