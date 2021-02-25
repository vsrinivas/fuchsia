// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/time.h>

#include <optional>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

#include "moniker_url_cache.h"

using MonikerCacheTest = gtest::TestLoopFixture;

TEST_F(MonikerCacheTest, AddAndGet) {
  MonikerUrlCache cache(zx::sec(10), dispatcher());
  cache.Add("my_moniker", "my_url");
  ASSERT_EQ(*cache.GetTestUrl("my_moniker"), "my_url");
  ASSERT_FALSE(cache.GetTestUrl("other_moniker").has_value());
}

TEST_F(MonikerCacheTest, Cleanup) {
  MonikerUrlCache cache(zx::sec(10), dispatcher());
  cache.Add("my_moniker", "my_url");
  RunLoopFor(zx::sec(8));

  // this should not have been deleted.
  ASSERT_EQ(*cache.GetTestUrl("my_moniker"), "my_url");
  RunLoopFor(zx::sec(5));
  // was accessed 5 sec back, should not have been deleted.
  ASSERT_EQ(*cache.GetTestUrl("my_moniker"), "my_url");

  // Add one more entry and check that old one is deleted but new one is not.

  cache.Add("other_moniker", "other_url");
  RunLoopFor(zx::sec(13));
  // access new entry again so that it is not deleted.
  ASSERT_EQ(*cache.GetTestUrl("other_moniker"), "other_url");
  RunLoopFor(zx::sec(8));
  ASSERT_FALSE(cache.GetTestUrl("my_moniker").has_value());
  ASSERT_EQ(*cache.GetTestUrl("other_moniker"), "other_url");
}
