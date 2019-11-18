// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"

#include "tools/kazoo/test.h"

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

TEST(OutputUtil, CamelToSnake) {
  EXPECT_EQ(CamelToSnake(""), "");
  EXPECT_EQ(CamelToSnake("A"), "a");
  EXPECT_EQ(CamelToSnake("AA"), "aa");
  EXPECT_EQ(CamelToSnake("Aa"), "aa");
  EXPECT_EQ(CamelToSnake("Stuff"), "stuff");
  EXPECT_EQ(CamelToSnake("SomeThing"), "some_thing");
  EXPECT_EQ(CamelToSnake("SomeOtherThing"), "some_other_thing");
  EXPECT_EQ(CamelToSnake("someThing"), "some_thing");
  EXPECT_EQ(CamelToSnake("ThisIsASCII"), "this_is_ascii");
  EXPECT_EQ(CamelToSnake("getHTTPResponseCode"), "get_http_response_code");
  EXPECT_EQ(CamelToSnake("get2HTTPResponseCode"), "get2_http_response_code");
  EXPECT_EQ(CamelToSnake("HTTPResponseCode"), "http_response_code");
  EXPECT_EQ(CamelToSnake("HTTPResponseCodeNEW"), "http_response_code_new");
  EXPECT_EQ(CamelToSnake("DoubleIEEE754"), "double_ieee754");
  EXPECT_EQ(CamelToSnake("MemVTable"), "mem_vtable");
  EXPECT_EQ(CamelToSnake("SList"), "slist");
  EXPECT_EQ(CamelToSnake("ThisIsASCII"), "this_is_ascii");
  EXPECT_EQ(CamelToSnake("ThisIsASCIIText"), "this_is_ascii_text");
  EXPECT_EQ(CamelToSnake("WaCkYsTuFf"), "wa_ck_ys_tu_ff");
  EXPECT_EQ(CamelToSnake("WAcK"), "wac_k");
}

TEST(DJBHash, DJBHash) {
  EXPECT_EQ(DJBHash(""), 5381u);
  EXPECT_EQ(DJBHash("zircon rocks"), 259778556u);
}

}  // namespace
