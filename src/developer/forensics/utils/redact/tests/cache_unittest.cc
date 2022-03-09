// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/redact/cache.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace forensics {
namespace {

TEST(RedactionIdCacheTest, GetId) {
  RedactionIdCache cache;
  EXPECT_EQ(cache.GetId("value1"), 1);
  EXPECT_EQ(cache.GetId("value1"), 1);
  EXPECT_EQ(cache.GetId("value1"), 1);

  EXPECT_EQ(cache.GetId("value2"), 2);
  EXPECT_EQ(cache.GetId("value2"), 2);
  EXPECT_EQ(cache.GetId("value2"), 2);

  EXPECT_EQ(cache.GetId("value3"), 3);
  EXPECT_EQ(cache.GetId("value3"), 3);
  EXPECT_EQ(cache.GetId("value3"), 3);

  EXPECT_EQ(cache.GetId("value4"), 4);
  EXPECT_EQ(cache.GetId("value4"), 4);
  EXPECT_EQ(cache.GetId("value4"), 4);
}

}  // namespace
}  // namespace forensics
