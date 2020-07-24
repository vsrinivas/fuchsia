// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/moniker.h"

#include <gtest/gtest.h>

namespace component {
namespace {

TEST(MonikerTest, ToString) {
  EXPECT_EQ((Moniker{.url = "a", .realm_path = {"sys"}}.ToString()), "sys#a");
  EXPECT_EQ((Moniker{.url = "a", .realm_path = {"sys", "blah"}}.ToString()), "sys#blah#a");
}

TEST(MonikerTest, CompareLessThan) {
  EXPECT_LT((Moniker{.url = "a", .realm_path = {"sys"}}),
            (Moniker{.url = "b", .realm_path = {"sys"}}));
  EXPECT_LT((Moniker{.url = "a", .realm_path = {"sys"}}),
            (Moniker{.url = "a", .realm_path = {"sys", "blah"}}));
}

}  // namespace
}  // namespace component
