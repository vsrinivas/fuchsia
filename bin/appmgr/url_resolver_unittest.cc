// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/url_resolver.h"

#include "gtest/gtest.h"

namespace component {
namespace {

TEST(URLResolver, GetSchemeFromURL) {
  EXPECT_EQ("", GetSchemeFromURL(""));
  EXPECT_EQ("", GetSchemeFromURL("abc"));
  EXPECT_EQ("abc", GetSchemeFromURL("abc:efg"));
  EXPECT_EQ("abc", GetSchemeFromURL("AbC:EfG"));
  EXPECT_EQ(" sdkfj kjfd @($*) ",
            GetSchemeFromURL(" sdkfj KJfd @($*) : foo baedf"));
}

}  // namespace
}  // namespace component
