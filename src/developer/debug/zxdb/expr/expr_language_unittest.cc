// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_language.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(ExprLanguage, FileNameToLanguage) {
  EXPECT_EQ(ExprLanguage::kC, *FileNameToLanguage("foo/bar.cc"));
  EXPECT_EQ(ExprLanguage::kC, *FileNameToLanguage("foo/bar.h"));
  EXPECT_EQ(std::nullopt, FileNameToLanguage("foo/bar."));
  EXPECT_EQ(std::nullopt, FileNameToLanguage("foo/bar"));
  EXPECT_EQ(std::nullopt, FileNameToLanguage("foo"));
  EXPECT_EQ(std::nullopt, FileNameToLanguage(""));

  EXPECT_EQ(ExprLanguage::kRust, *FileNameToLanguage("foo.rs"));
  EXPECT_EQ(ExprLanguage::kRust, *FileNameToLanguage("foo/bar.rs"));
}

}  // namespace zxdb
