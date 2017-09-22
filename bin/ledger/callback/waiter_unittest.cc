// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/callback/waiter.h"
#include "gtest/gtest.h"

namespace callback {
namespace {

TEST(Waiter, NoCallback) {
  auto waiter = Waiter<int, int>::Create(0);

  int result = -1;
  std::vector<int> data;
  waiter->Finalize(Capture([] {}, &result, &data));

  EXPECT_EQ(0, result);
  EXPECT_EQ(std::vector<int>(), data);
}

TEST(Waiter, DataPreInitialize) {
  auto waiter = Waiter<int, int>::Create(0);

  waiter->NewCallback()(0, 0);
  waiter->NewCallback()(0, 1);
  waiter->NewCallback()(0, 2);

  int result = -1;
  std::vector<int> data;
  waiter->Finalize(Capture([] {}, &result, &data));

  EXPECT_EQ(0, result);
  EXPECT_EQ(std::vector<int>({0, 1, 2}), data);
}

TEST(Waiter, DataPostInitialize) {
  auto waiter = Waiter<int, int>::Create(0);

  auto c1 = waiter->NewCallback();
  auto c2 = waiter->NewCallback();
  auto c3 = waiter->NewCallback();

  int result = -1;
  std::vector<int> data;
  waiter->Finalize(Capture([] {}, &result, &data));

  EXPECT_EQ(-1, result);
  c1(0, 0);
  EXPECT_EQ(-1, result);
  c2(0, 1);
  EXPECT_EQ(-1, result);
  c3(0, 2);

  EXPECT_EQ(0, result);
  EXPECT_EQ(std::vector<int>({0, 1, 2}), data);
}

TEST(Waiter, DataMixedInitialize) {
  auto waiter = Waiter<int, int>::Create(0);

  waiter->NewCallback()(0, 0);
  waiter->NewCallback()(0, 1);

  auto c = waiter->NewCallback();

  int result = -1;
  std::vector<int> data;
  waiter->Finalize(Capture([] {}, &result, &data));

  EXPECT_EQ(-1, result);

  c(0, 2);

  EXPECT_EQ(0, result);
  EXPECT_EQ(std::vector<int>({0, 1, 2}), data);
}

TEST(Waiter, UnorderedCalls) {
  auto waiter = Waiter<int, int>::Create(0);

  auto c1 = waiter->NewCallback();
  auto c2 = waiter->NewCallback();
  auto c3 = waiter->NewCallback();

  c2(0, 1);
  c3(0, 2);
  c1(0, 0);

  int result = -1;
  std::vector<int> data;
  waiter->Finalize(Capture([] {}, &result, &data));

  EXPECT_EQ(0, result);
  EXPECT_EQ(std::vector<int>({0, 1, 2}), data);
}

TEST(Waiter, EarlyReturnOnError) {
  auto waiter = Waiter<int, int>::Create(0);

  waiter->NewCallback();
  waiter->NewCallback()(1, 2);
  waiter->NewCallback();

  int result = -1;
  std::vector<int> data;
  waiter->Finalize(Capture([] {}, &result, &data));

  EXPECT_EQ(1, result);
  EXPECT_EQ(std::vector<int>(), data);
}

TEST(Waiter, CallbackSurviveWaiter) {
  auto waiter = Waiter<int, int>::Create(0);
  auto c1 = waiter->NewCallback();

  waiter = nullptr;

  c1(0, 0);
}

TEST(Waiter, Promise) {
  auto promise = Promise<int, int>::Create(0);

  promise->NewCallback()(1, 2);
  int status, result;
  promise->Finalize(Capture([] {}, &status, &result));
  EXPECT_EQ(1, status);
  EXPECT_EQ(2, result);
}

TEST(StatusWaiter, MixedInitialize) {
  auto waiter = StatusWaiter<int>::Create(0);

  waiter->NewCallback()(0);
  waiter->NewCallback()(0);
  auto c = waiter->NewCallback();

  int result = -1;
  waiter->Finalize(Capture([] {}, &result));

  EXPECT_EQ(-1, result);

  c(0);
  EXPECT_EQ(0, result);
}

TEST(StatusWaiter, EarlyReturnOnError) {
  auto waiter = StatusWaiter<int>::Create(0);

  waiter->NewCallback()(0);
  waiter->NewCallback()(1);

  // Create callback, but do not call it.
  waiter->NewCallback();

  int result = -1;
  waiter->Finalize(Capture([] {}, &result));

  EXPECT_EQ(1, result);
}

TEST(CompletionWaiter, MixedInitialize) {
  auto waiter = CompletionWaiter::Create();

  waiter->NewCallback()();
  waiter->NewCallback()();
  auto c = waiter->NewCallback();

  bool called = false;
  waiter->Finalize([&called] { called = true; });

  EXPECT_FALSE(called);

  c();

  EXPECT_TRUE(called);
}

}  //  namespace
}  //  namespace callback
