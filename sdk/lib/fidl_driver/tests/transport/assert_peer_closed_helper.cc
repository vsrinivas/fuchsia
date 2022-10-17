// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/fidl_driver/tests/transport/assert_peer_closed_helper.h"

#include <zxtest/zxtest.h>

namespace fidl_driver_testing {

// Generates a test failure if the peer of |channel| is not closed.
void AssertPeerClosed(const zx::channel& channel) {
  zx_signals_t observed = 0;
  zx_status_t status =
      channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite_past(), &observed);
  if (status != ZX_OK || observed != ZX_CHANNEL_PEER_CLOSED) {
    FAIL("Zircon channel peer is not closed (%d)", status);
  }
}

// Generates a test failure if the peer of |channel| is not closed.
void AssertPeerClosed(const fdf::Channel& channel) {
  zx::result status = channel.Read(0);
  if (status.status_value() != ZX_ERR_PEER_CLOSED) {
    FAIL("Driver channel peer is not closed (%d)", status.status_value());
  }
}

}  // namespace fidl_driver_testing
