// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/client.h>

#include <zxtest/zxtest.h>

namespace {

TEST(TeardownObserver, ObserveTeardown) {
  bool called = false;
  {
    fidl::internal::AnyTeardownObserver observer = fidl::ObserveTeardown([&called]() {
      ASSERT_FALSE(called);
      called = true;
    });
    ASSERT_FALSE(called);
    std::move(observer).Notify();
    ASSERT_TRUE(called);
  }
  ASSERT_TRUE(called);
}

TEST(TeardownObserver, ShareUntilTeardown) {
  std::shared_ptr p = std::make_shared<int>();
  EXPECT_EQ(1, p.use_count());
  {
    fidl::internal::AnyTeardownObserver observer = fidl::ShareUntilTeardown(p);
    EXPECT_EQ(2, p.use_count());
    std::move(observer).Notify();
    EXPECT_EQ(1, p.use_count());
  }
  EXPECT_EQ(1, p.use_count());
}

// Mock user object class used for testing.
class LifetimeTracker {
 public:
  explicit LifetimeTracker(bool* alive) : alive_(alive) { *alive_ = true; }
  ~LifetimeTracker() { *alive_ = false; }

 private:
  bool* alive_;
};

TEST(TeardownObserver, OwnUntilTeardown) {
  bool alive;
  std::unique_ptr p = std::make_unique<LifetimeTracker>(&alive);
  ASSERT_TRUE(alive);
  {
    auto observer = fidl::internal::AnyTeardownObserver::ByOwning(std::move(p));
    ASSERT_TRUE(alive);
    std::move(observer).Notify();
    ASSERT_FALSE(alive);
  }
  ASSERT_FALSE(alive);
}

TEST(AnyTeardownObserver, CannotNotifyTwice) {
  auto observer = fidl::internal::AnyTeardownObserver::ByCallback([] {});
  std::move(observer).Notify();
  ASSERT_DEATH([&] { std::move(observer).Notify(); });
}

}  // namespace
