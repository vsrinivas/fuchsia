// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/managed_container.h"

#include "gtest/gtest.h"
#include "lib/callback/set_when_called.h"
#include "lib/fxl/functional/auto_call.h"

namespace callback {
namespace {

TEST(ManagedContainer, Cleanup) {
  ManagedContainer managed_container;
  size_t called = 0;
  auto result =
      managed_container.Manage(fxl::MakeAutoCall([&called] { ++called; }));
  EXPECT_EQ(0u, called);
  result.reset();
  EXPECT_EQ(1u, called);
}

TEST(ManagedContainer, ContainerDeletion) {
  size_t called = 0;
  auto updater = fxl::MakeAutoCall([&called] { ++called; });
  auto managed_container = std::make_unique<ManagedContainer>();
  auto result = managed_container->Manage(std::move(updater));
  EXPECT_EQ(0u, called);
  managed_container.reset();
  EXPECT_EQ(1u, called);
}

TEST(ManagedContainer, HandlerDeletion) {
  size_t called = 0;
  auto updater = fxl::MakeAutoCall([&called] { ++called; });
  ManagedContainer managed_container;
  {
    auto result = managed_container.Manage(std::move(updater));
    EXPECT_EQ(0u, called);
  }
  EXPECT_EQ(1u, called);
}

TEST(ManagedContainer, HeterogenousObject) {
  ManagedContainer managed_container;
  size_t called = 0;
  auto result1 =
      managed_container.Manage(fxl::MakeAutoCall([&called] { ++called; }));
  auto result2 =
      managed_container.Manage(fxl::MakeAutoCall([&called] { ++called; }));
  EXPECT_EQ(0u, called);
  result1.reset();
  EXPECT_EQ(1u, called);
  result2.reset();
  EXPECT_EQ(2u, called);
}

TEST(ManagedContainer, DoNotCrashIfManagerDeleted) {
  std::unique_ptr<ManagedContainer> managed_container =
      std::make_unique<ManagedContainer>();
  size_t called = 0;
  auto result =
      managed_container->Manage(fxl::MakeAutoCall([&called] { ++called; }));
  EXPECT_EQ(0u, called);
  managed_container.reset();
  // |managed_container| is deleted and all its storage.
  EXPECT_EQ(1u, called);
  result.reset();
  // Nothing bad should happen
}

TEST(ManagedContainer, OnEmpty) {
  ManagedContainer managed_container;
  bool called = false;
  managed_container.set_on_empty(SetWhenCalled(&called));
  auto item1 = managed_container.Manage(true);
  auto item2 = managed_container.Manage(true);
  item1.reset();
  EXPECT_FALSE(called);
  item2.reset();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace callback
