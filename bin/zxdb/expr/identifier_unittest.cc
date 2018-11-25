// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/identifier.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(Identifier, GetFullName) {
  // Empty.
  Identifier unqualified;
  EXPECT_EQ("", unqualified.GetFullName());

  // Single name with no "::" at the beginning.
  unqualified.AppendComponent(
      ExprToken(), ExprToken(ExprToken::kName, "First", 2));
  EXPECT_EQ("First", unqualified.GetFullName());

  // Single name with a "::" at the beginning.
  Identifier qualified;
  qualified.AppendComponent(ExprToken(ExprToken::kColonColon, "::", 0),
                            ExprToken(ExprToken::kName, "First", 2));
  EXPECT_EQ("::First", qualified.GetFullName());

  // Append some template stuff.
  qualified.AppendComponent(ExprToken(ExprToken::kColonColon, "::", 7),
                            ExprToken(ExprToken::kName, "Second", 9),
                            ExprToken(ExprToken::kLess, "<", 15),
                            {"int", "Foo"},
                            ExprToken(ExprToken::kGreater, ">", 24));
  EXPECT_EQ("::First::Second<int, Foo>", qualified.GetFullName());
}

}  // namespace zxdb
