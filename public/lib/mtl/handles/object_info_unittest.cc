// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/handles/object_info.h"

#include <magenta/syscalls/object.h>
#include <mx/event.h>

#include "gtest/gtest.h"

namespace mtl {
namespace {

TEST(GetKoid, InvalidHandle) {
  EXPECT_EQ(MX_KOID_INVALID, GetKoid(MX_HANDLE_INVALID));
}

TEST(GetKoid, ValidHandlesForDistinctObjects) {
  mx::event event1, event2;
  ASSERT_EQ(NO_ERROR, mx::event::create(0u, &event1));
  ASSERT_EQ(NO_ERROR, mx::event::create(0u, &event2));

  EXPECT_NE(MX_KOID_INVALID, GetKoid(event1.get()));
  EXPECT_NE(MX_KOID_INVALID, GetKoid(event2.get()));
  EXPECT_NE(GetKoid(event1.get()), GetKoid(event2.get()));
}

TEST(GetKoid, ValidHandlesForSameObject) {
  mx::event event1, event2;
  ASSERT_EQ(NO_ERROR, mx::event::create(0u, &event1));
  ASSERT_EQ(NO_ERROR, event1.duplicate(MX_RIGHT_SAME_RIGHTS, &event2));

  EXPECT_NE(MX_KOID_INVALID, GetKoid(event1.get()));
  EXPECT_EQ(GetKoid(event1.get()), GetKoid(event2.get()));
}

}  // namespace
}  // namespace mtl
