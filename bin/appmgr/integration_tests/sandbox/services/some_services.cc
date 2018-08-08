// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/integration_tests/sandbox/namespace_test.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/testing/appmgr/cpp/fidl.h>
#include <zircon/errors.h>
#include "gtest/gtest.h"

TEST_F(NamespaceTest, SomeServices) {
  // Only whitelisted service is available.
  fuchsia::testing::appmgr::TestServiceSyncPtr test_service;
  fuchsia::testing::appmgr::TestService2SyncPtr test_service2;
  ConnectToService(test_service.NewRequest());
  ConnectToService(test_service2.NewRequest());
  RunLoopUntilIdle();

  ::fidl::StringPtr message, message2;
  ASSERT_EQ(ZX_OK, test_service->GetMessage(&message));
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, test_service2->GetMessage(&message2));
  EXPECT_EQ(message.get(), "hello");
  EXPECT_EQ(message2.get(), "");
}
