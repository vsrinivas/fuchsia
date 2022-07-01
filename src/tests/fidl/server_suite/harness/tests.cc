// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harness.h"
#include "ordinals.h"

TEST_F(ServerTest, TestSetUp_Success) {}

TEST_F(ServerTest, OneWayInteraction_Success) {
  fidl_message_header_t hdr;
  fidl::InitTxnHeader(&hdr, 0, kOrdinalOneWayInteractionNoPayload,
                      fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client_end().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  WAIT_UNTIL([this]() { return reporter().received_one_way_no_payload(); });
}

TEST_F(ServerTest, WrongOrdinalCausesUnbind_Success) {
  fidl_message_header_t hdr;
  fidl::InitTxnHeader(&hdr, 0, /* some wrong ordinal */ 8888888lu,
                      fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client_end().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  ASSERT_EQ(ZX_OK,
            client_end().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::sec(5)), nullptr));
}
