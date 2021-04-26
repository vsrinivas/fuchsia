// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <lib/sys/cpp/testing/realm_builder.h>

namespace {

TEST(RealmBuilderTest, HelloWorld) {
    auto realm_builder = sys::testing::RealmBuilder();
    realm_builder.AddComponent();

    EXPECT_EQ(2 + 2, 4);
}


}  // namespace
