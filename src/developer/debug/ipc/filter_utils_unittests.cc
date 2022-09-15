// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/ipc/filter_utils.h"
#include "src/developer/debug/ipc/records.h"

namespace debug_ipc {

TEST(FilterUtils, FilterMatches) {
  Filter filter{.type = debug_ipc::Filter::Type::kProcessName, .pattern = "foo"};
  EXPECT_TRUE(FilterMatches(filter, "foo", std::nullopt));
  EXPECT_FALSE(FilterMatches(filter, "foobar", std::nullopt));

  filter = {.type = debug_ipc::Filter::Type::kProcessNameSubstr, .pattern = "foo"};
  EXPECT_TRUE(FilterMatches(filter, "foo", std::nullopt));
  EXPECT_TRUE(FilterMatches(filter, "foobar", std::nullopt));

  filter = {.type = debug_ipc::Filter::Type::kComponentMoniker, .pattern = "/core/abc"};
  EXPECT_TRUE(FilterMatches(filter, "", ComponentInfo{.moniker = "/core/abc"}));
  EXPECT_FALSE(FilterMatches(filter, "", ComponentInfo{.moniker = "/core/abc/def"}));

  filter = {.type = debug_ipc::Filter::Type::kComponentName, .pattern = "foo.cm"};
  EXPECT_TRUE(FilterMatches(filter, "", ComponentInfo{.url = "pkg://host#meta/foo.cm"}));

  filter = {.type = debug_ipc::Filter::Type::kComponentUrl, .pattern = "pkg://host#meta/foo.cm"};
  EXPECT_TRUE(FilterMatches(filter, "", ComponentInfo{.url = "pkg://host?hash=abcd#meta/foo.cm"}));
}

}  // namespace debug_ipc
