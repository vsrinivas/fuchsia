// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/file_util.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(FileUtil, ExtractLastFileComponent) {
  EXPECT_EQ("", ExtractLastFileComponent(""));
  EXPECT_EQ("", ExtractLastFileComponent("foo/"));
  EXPECT_EQ("foo.cpp", ExtractLastFileComponent("foo.cpp"));
  EXPECT_EQ("foo.cpp", ExtractLastFileComponent("bar/foo.cpp"));
  EXPECT_EQ("foo.cpp", ExtractLastFileComponent("baz/bar/foo.cpp"));
}

}  // namespace zxdb
