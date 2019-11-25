// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/object.h>

#include <assert.h>
#include <lib/zx/event.h>

#include <set>

#include <zxtest/zxtest.h>

namespace {

TEST(ZxUnowned, UsableInContainers) {
  std::set<zx::unowned_event> set;
  zx::event event;
  ASSERT_OK(zx::event::create(0u, &event));
  set.emplace(event);
  EXPECT_EQ(set.size(), 1u);
  EXPECT_EQ(*set.begin(), event.get());
}

TEST(ZxObject, BorrowReturnsUnownedObjectOfSameHandle) {
  zx::event event;
  ASSERT_OK(zx::event::create(0u, &event));

  ASSERT_EQ(event.get(), event.borrow());
  ASSERT_EQ(zx::unowned_event(event), event.borrow());
}

}  // namespace
