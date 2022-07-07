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
  fidl::InitTxnHeader(&hdr, 0, kOrdinalOneWayInteractionNoPayload,
                      fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client_end().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  WAIT_UNTIL([this]() { return reporter().received_one_way_no_payload(); });
}

// Check that the channel is closed when a new one-way request with a non-zero txid is received.
SERVER_TEST(OneWayWithNonZeroTxid) {
  fidl_message_header_t hdr;
  fidl::InitTxnHeader(&hdr, 56 /* txid not 0 */, kOrdinalOneWayInteractionNoPayload,
                      fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client_end().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  ASSERT_EQ(ZX_OK,
            client_end().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::sec(5)), nullptr));
}

// Check that the server closes the channel when unknown ordinals are received.
SERVER_TEST(UnknownOrdinalCausesClose) {
  fidl_message_header_t hdr;
  fidl::InitTxnHeader(&hdr, 0, /* some wrong ordinal */ 8888888lu,
                      fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client_end().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  ASSERT_EQ(ZX_OK,
            client_end().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::sec(5)), nullptr));
}
