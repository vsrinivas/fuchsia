// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tests/fidl/client_suite/cpp_sync/runner.h"

CLIENT_TEST(Setup) {}

CLIENT_TEST(ServerClosesChannel) {
  ASSERT_OK(target().client_end().channel().wait_one(
      ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(kTimeoutDuration), nullptr));
}

CLIENT_TEST(TwoWayNoPayload) {
  auto result = target()->TwoWayNoPayload();
  ASSERT_TRUE(result.is_ok()) << result.error_value();
}
