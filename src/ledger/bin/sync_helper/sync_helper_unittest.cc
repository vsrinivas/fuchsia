// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/sync_helper/sync_helper.h"

#include "gtest/gtest.h"
#include "src/ledger/lib/callback/set_when_called.h"

namespace ledger {
namespace {

TEST(SyncHelper, NoOperation) {
  SyncHelper sync_helper;
  bool called = false;
  sync_helper.RegisterSynchronizationCallback(SetWhenCalled(&called));
  EXPECT_TRUE(called);
}

TEST(SyncHelper, OneOperation) {
  SyncHelper sync_helper;
  auto operation = sync_helper.WrapOperation([] {});
  bool called = false;
  sync_helper.RegisterSynchronizationCallback(SetWhenCalled(&called));
  EXPECT_FALSE(called);
  operation();
  EXPECT_TRUE(called);
}

TEST(SyncHelper, TwoSyncCallbacks) {
  SyncHelper sync_helper;
  auto operation = sync_helper.WrapOperation([] {});
  bool called1 = false;
  bool called2 = false;
  sync_helper.RegisterSynchronizationCallback(SetWhenCalled(&called1));
  sync_helper.RegisterSynchronizationCallback(SetWhenCalled(&called2));
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
  sync_helper.RegisterSynchronizationCallback(SetWhenCalled(&called));

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
  sync_helper.RegisterSynchronizationCallback(SetWhenCalled(&called));

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
  sync_helper.RegisterSynchronizationCallback(SetWhenCalled(&called1));
  auto operation2 = sync_helper.WrapOperation([] {});
  bool called2 = false;
  sync_helper.RegisterSynchronizationCallback(SetWhenCalled(&called2));

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

  EXPECT_EQ(operation_count, 0);
  EXPECT_EQ(called_count, 0);
  operation();
  EXPECT_EQ(operation_count, 1);
  EXPECT_EQ(called_count, 1);
  operation();
  EXPECT_EQ(operation_count, 2);
  EXPECT_EQ(called_count, 1);
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
  const auto operation = sync_helper.WrapOperation(SetWhenCalled(&called));
  EXPECT_FALSE(called);
  operation();
  EXPECT_TRUE(called);
}

TEST(SyncHelper, OnDiscardableCallback) {
  SyncHelper sync_helper;
  bool on_discardable_called;
  sync_helper.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  EXPECT_TRUE(sync_helper.IsDiscardable());
  const auto operation = sync_helper.WrapOperation([] {});
  EXPECT_FALSE(on_discardable_called);
  EXPECT_FALSE(sync_helper.IsDiscardable());
  operation();
  EXPECT_TRUE(on_discardable_called);
  EXPECT_TRUE(sync_helper.IsDiscardable());
}

TEST(SyncHelper, SyncWithDeletedOperation) {
  SyncHelper sync_helper;
  bool called;
  fit::closure operation = sync_helper.WrapOperation([] {});
  sync_helper.RegisterSynchronizationCallback(SetWhenCalled(&called));
  EXPECT_FALSE(called);
  operation = nullptr;
  EXPECT_TRUE(called);
}

TEST(SyncHelper, OnDiscardableWithDeletedOperation) {
  SyncHelper sync_helper;
  bool on_discardable_called;
  sync_helper.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  fit::closure operation = sync_helper.WrapOperation([] {});
  EXPECT_FALSE(on_discardable_called);
  operation = nullptr;
  EXPECT_TRUE(on_discardable_called);
}

}  // namespace
}  // namespace ledger
