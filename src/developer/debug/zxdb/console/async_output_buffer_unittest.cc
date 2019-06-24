// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/async_output_buffer.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

TEST(AsyncOutputBuffer, Empty) {
  auto buf = fxl::MakeRefCounted<AsyncOutputBuffer>();
  EXPECT_FALSE(buf->is_complete());

  buf->Complete();
  EXPECT_TRUE(buf->is_complete());

  auto flat = buf->DestructiveFlatten();
  EXPECT_TRUE(flat.empty());
}

TEST(AsyncOutputBuffer, OneLevel) {
  auto buf = fxl::MakeRefCounted<AsyncOutputBuffer>();

  buf->Append("One ");
  buf->Complete("Two");
  EXPECT_TRUE(buf->is_complete());
  EXPECT_EQ("One Two", buf->DestructiveFlatten().AsString());
}

TEST(AsyncOutputBuffer, Sync) {
  auto level1 = fxl::MakeRefCounted<AsyncOutputBuffer>();

  level1->Append("[1 ");

  // Level 2 is appended before being marked complete.
  auto level2 = fxl::MakeRefCounted<AsyncOutputBuffer>();
  level1->Append(level2);
  level2->Append("[2 ");

  // Level 3 is complete before being appended.
  auto level3 = fxl::MakeRefCounted<AsyncOutputBuffer>();
  level3->Complete("[3]]");
  EXPECT_TRUE(level3->is_complete());

  EXPECT_FALSE(level2->is_complete());
  level2->Complete(level3);
  EXPECT_TRUE(level2->is_complete());

  EXPECT_FALSE(level1->is_complete());
  level1->Complete(OutputBuffer("]"));
  EXPECT_TRUE(level1->is_complete());

  auto flat = level1->DestructiveFlatten();
  EXPECT_EQ("[1 [2 [3]]]", flat.AsString());
}

TEST(AsyncOutputBuffer, Async) {
  auto level1 = fxl::MakeRefCounted<AsyncOutputBuffer>();

  bool level1_complete = false;
  level1->SetCompletionCallback([&level1, &level1_complete]() {
    EXPECT_TRUE(level1->is_complete());
    level1_complete = true;
  });

  level1->Append("[1 ");

  // Level 2 is appended before being marked complete.
  auto level2 = fxl::MakeRefCounted<AsyncOutputBuffer>();
  level1->Append(level2);
  level1->Complete(OutputBuffer("]"));
  level2->Append("[2 ");

  // Level 3 is complete before being appended.
  auto level3 = fxl::MakeRefCounted<AsyncOutputBuffer>();
  level3->Complete("[3]]");
  EXPECT_TRUE(level3->is_complete());

  // Level 1 callback should not have been issued yet.
  EXPECT_FALSE(level1_complete);

  EXPECT_FALSE(level2->is_complete());
  level2->Complete(level3);
  EXPECT_TRUE(level2->is_complete());

  // Level 1 callback should have been issued.
  EXPECT_TRUE(level1_complete);
  EXPECT_TRUE(level1->is_complete());

  auto flat = level1->DestructiveFlatten();
  EXPECT_EQ("[1 [2 [3]]]", flat.AsString());
}

}  // namespace zxdb
