// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/pending_operation.h"

#include "gtest/gtest.h"
#include "lib/ftl/functional/auto_call.h"

namespace callback {
namespace {

TEST(PendingOperationManager, Cleanup) {
  PendingOperationManager operation_manager;
  size_t called = 0;
  auto result =
      operation_manager.Manage(ftl::MakeAutoCall([&called] { ++called; }));
  EXPECT_EQ(0u, called);
  result.second();
  EXPECT_EQ(1u, called);
}

TEST(PendingOperationManager, Deletion) {
  size_t called = 0;
  auto updater = ftl::MakeAutoCall([&called] { ++called; });
  {
    PendingOperationManager operation_manager;
    operation_manager.Manage(std::move(updater));
    EXPECT_EQ(0u, called);
  }
  EXPECT_EQ(1u, called);
}

TEST(PendingOperationManager, HeterogenousObject) {
  PendingOperationManager operation_manager;
  size_t called = 0;
  auto result1 =
      operation_manager.Manage(ftl::MakeAutoCall([&called] { ++called; }));
  auto result2 =
      operation_manager.Manage(ftl::MakeAutoCall([&called] { ++called; }));
  EXPECT_EQ(0u, called);
  result1.second();
  EXPECT_EQ(1u, called);
  result2.second();
  EXPECT_EQ(2u, called);
}

}  // namespace
}  // namespace callback
