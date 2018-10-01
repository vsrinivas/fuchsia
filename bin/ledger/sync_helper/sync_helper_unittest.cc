// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/sync_helper/sync_helper.h"

#include "gtest/gtest.h"

namespace ledger {
namespace {

TEST(SyncHelper, NoOperation) {
  SyncHelper sync_helper;
  bool called = false;
  sync_helper.RegisterSynchronizationCallback([&] { called = true; });
  EXPECT_TRUE(called);
}

TEST(SyncHelper, OneOperation) {
  SyncHelper sync_helper;
  auto operation = sync_helper.WrapOperation([] {});
  bool called = false;
  sync_helper.RegisterSynchronizationCallback([&] { called = true; });
  EXPECT_FALSE(called);
  operation();
  EXPECT_TRUE(called);
}

TEST(SyncHelper, TwoSyncCallbacks) {
  SyncHelper sync_helper;
  auto operation = sync_helper.WrapOperation([] {});
  bool called1 = false;
  bool called2 = false;
  sync_helper.RegisterSynchronizationCallback([&] { called1 = true; });
  sync_helper.RegisterSynchronizationCallback([&] { called2 = true; });
  EXPECT_FALSE(called1);
  EXPECT_FALSE(called2);
  operation();
  EXPECT_TRUE(called1);
  EXPECT_TRUE(called2);
}

TEST(SyncHelper, TwoOperation) {
  SyncHelper sync_helper;
  auto operation1 = sync_helper.WrapOperation([] {});
  auto operation2 = sync_helper.WrapOperation([] {});
  bool called = false;
  sync_helper.RegisterSynchronizationCallback([&] { called = true; });

  EXPECT_FALSE(called);
  operation1();
  EXPECT_FALSE(called);
  operation2();
  EXPECT_TRUE(called);
}

TEST(SyncHelper, TwoOperationReversed) {
  SyncHelper sync_helper;
  auto operation1 = sync_helper.WrapOperation([] {});
  auto operation2 = sync_helper.WrapOperation([] {});
  bool called = false;
  sync_helper.RegisterSynchronizationCallback([&] { called = true; });

  EXPECT_FALSE(called);
  operation2();
  EXPECT_FALSE(called);
  operation1();
  EXPECT_TRUE(called);
}

TEST(SyncHelper, TwoOperationTwoCallbacks) {
  SyncHelper sync_helper;
  auto operation1 = sync_helper.WrapOperation([] {});
  bool called1 = false;
  sync_helper.RegisterSynchronizationCallback([&] { called1 = true; });
  auto operation2 = sync_helper.WrapOperation([] {});
  bool called2 = false;
  sync_helper.RegisterSynchronizationCallback([&] { called2 = true; });

  EXPECT_FALSE(called1);
  EXPECT_FALSE(called2);
  operation1();
  EXPECT_TRUE(called1);
  EXPECT_FALSE(called2);
  operation2();
  EXPECT_TRUE(called1);
  EXPECT_TRUE(called2);
}

TEST(SyncHelper, CallOperationTwice) {
  SyncHelper sync_helper;
  int operation_count = 0;
  auto operation = sync_helper.WrapOperation([&] { ++operation_count; });
  int called_count = 0;
  sync_helper.RegisterSynchronizationCallback([&] { ++called_count; });

  EXPECT_EQ(0, operation_count);
  EXPECT_EQ(0, called_count);
  operation();
  EXPECT_EQ(1, operation_count);
  EXPECT_EQ(1, called_count);
  operation();
  EXPECT_EQ(2, operation_count);
  EXPECT_EQ(1, called_count);
}

TEST(SyncHelper, WrapMutableLambda) {
  SyncHelper sync_helper;
  bool called = false;
  sync_helper.WrapOperation([&called]() mutable { called = true; })();
  EXPECT_TRUE(called);
}

TEST(SyncHelper, StoreConstWrappedOperation) {
  SyncHelper sync_helper;
  bool called = false;
  const auto operation =
      sync_helper.WrapOperation([&called]() { called = true; });
  EXPECT_FALSE(called);
  operation();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace ledger
