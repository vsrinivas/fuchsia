// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/output_buffer.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(OutputBuffer, TrimTrailingNewlines) {
  OutputBuffer empty;
  empty.TrimTrailingNewlines();
  EXPECT_EQ("", empty.GetDebugString());

  OutputBuffer only_newlines;
  only_newlines.Append("\n\n");
  only_newlines.Append(Syntax::kComment, "\n");
  only_newlines.TrimTrailingNewlines();
  EXPECT_EQ("", only_newlines.GetDebugString());

  OutputBuffer etc;
  etc.Append("\nFoo\n");
  etc.Append(Syntax::kComment, "\n");
  etc.TrimTrailingNewlines();
  EXPECT_EQ("kNormal \"\nFoo\"", etc.GetDebugString());

  OutputBuffer no_newline("hello");
  no_newline.TrimTrailingNewlines();
  EXPECT_EQ("kNormal \"hello\"", no_newline.GetDebugString());
}

}  // namespace zxdb
