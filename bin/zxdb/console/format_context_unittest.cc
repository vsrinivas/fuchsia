// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_context.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "gtest/gtest.h"

namespace zxdb {

static const char kSimpleProgram[] =
    R"(#include "foo.h"

int main(int argc, char** argv) {
  printf("Hello, world");
  return 1;
}
)";

TEST(FormatFileContext, Basic) {
  OutputBuffer out;
  ASSERT_TRUE(FormatFileContext(kSimpleProgram, 4, 11, &out));
  EXPECT_EQ(R"(  2  
  3  int main(int argc, char** argv) {
▶ 4    printf("Hello, world");
  5    return 1;
  6  }
)",
            out.AsString());
}

TEST(FormatFileContext, ContextOffBeginning) {
  OutputBuffer out;
  // This column is off the end of line two, and the context has one less line
  // at the beginning because it hit the top of the file.
  ASSERT_TRUE(FormatFileContext(kSimpleProgram, 2, 11, &out));
  EXPECT_EQ(R"(  1  #include "foo.h"
▶ 2  
  3  int main(int argc, char** argv) {
  4    printf("Hello, world");
)",
            out.AsString());
}

TEST(FormatFileContext, ContextOffEnd) {
  OutputBuffer out;
  // This column is off the end of line two, and the context has one less line
  // at the beginning because it hit the top of the file.
  ASSERT_TRUE(FormatFileContext(kSimpleProgram, 6, 0, &out));
  EXPECT_EQ(R"(  4    printf("Hello, world");
  5    return 1;
▶ 6  }
)",
            out.AsString());
}

TEST(FormatFileContext, LineOffEnd) {
  OutputBuffer out;
  // This line is off the end of the input.
  EXPECT_FALSE(FormatFileContext(kSimpleProgram, 10, 0, &out));
}

}  // namespace zxdb
