// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"

#include "gtest/gtest.h"

namespace {

class OverrideWriter : public Writer {
 public:
  bool Puts(const std::string& str) override {
    data_ += str;
    return true;
  }

  const std::string& data() const { return data_; }

 private:
  std::string data_;
};

TEST(OutputUtil, CppCopyrightHeader) {
  OverrideWriter writer;
  EXPECT_EQ(CopyrightHeaderWithCppComments(&writer), true);
  ASSERT_TRUE(writer.data().size() > 2);
  EXPECT_EQ(writer.data()[0], '/');
  EXPECT_EQ(writer.data()[1], '/');
  EXPECT_EQ(writer.data().back(), '\n');
}

TEST(OutputUtil, HashCopyrightHeader) {
  OverrideWriter writer;
  EXPECT_EQ(CopyrightHeaderWithHashComments(&writer), true);
  ASSERT_TRUE(writer.data().size() > 1);
  EXPECT_EQ(writer.data()[0], '#');
  EXPECT_EQ(writer.data().back(), '\n');
}

}  // namespace
