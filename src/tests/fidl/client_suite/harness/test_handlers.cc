// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tests/fidl/client_suite/harness/harness.h"
#include "src/tests/fidl/client_suite/harness/ordinals.h"

using namespace channel_util;

TEST_HANDLER(Setup) {}

TEST_HANDLER(GracefulFailureDuringCallAfterPeerClose) { channel().reset(); }

TEST_HANDLER(TwoWayNoPayload) {
  VERIFY_OK(channel().wait_for_signal(ZX_CHANNEL_READABLE));

  zx_txid_t txid;
  channel_util::Bytes bytes_in = {
      header(0, kOrdinalTwoWayNoPayload, fidl::MessageDynamicFlags::kStrictMethod),
  };
  VERIFY_OK(channel().read_and_check_unknown_txid(&txid, bytes_in));

  channel_util::Bytes bytes_out = {
      header(txid, kOrdinalTwoWayNoPayload, fidl::MessageDynamicFlags::kStrictMethod),
  };
  VERIFY_OK(channel().write(bytes_out));
}
