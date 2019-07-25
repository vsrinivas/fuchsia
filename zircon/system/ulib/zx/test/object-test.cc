// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>

#include <lib/zx/event.h>
#include <lib/zx/object.h>
#include <zxtest/zxtest.h>

#include <set>

namespace {

TEST(ZxUnowned, UsableInContainers) {
  std::set<zx::unowned_event> set;
  zx::event event;
  ASSERT_OK(zx::event::create(0u, &event));
  set.emplace(event);
  EXPECT_EQ(set.size(), 1u);
  EXPECT_EQ(*set.begin(), event.get());
}

}  // namespace
