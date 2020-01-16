// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/shell/josh/console/console.h"
#include "src/developer/shell/josh/lib/js_testing_utils.h"

namespace shell {

class PpTest : public JsTest {
 protected:
  void SetUp() override { JsTest::SetUp(); }
};

TEST_F(PpTest, BasicPrettyPrint) {
  InitBuiltins("/pkg/data/fidling", "/pkg/data/lib");
  ctx_->Export("util", "/pkg/data/lib");

  std::string test_string = R"(
    {
      let data = { "a": 1, "b": 2.0, "c": "foo", "d" : true, "e" : [1,2,3] };
      let actual = pp.sprint(data, {quotes:true, whitespace:true});
      let expected =`{
 "a" : 1,
 "b" : 2,
 "c" : "foo",
 "d" : true,
 "e" : [
  1,
  2,
  3
 ]
}`;
      if (actual != expected) {
        throw "actual:\n" + actual + "\ndoes not match expected:\n" + expected;
      }
    }
    {
      let data = { "a": 1, "b": 2.0, "c": "foo", "d" : true };
      let actualCols = pp.scols([data]);
      let regex = /\s+a\s+b\s+c\s+c\s*\n\s+1\s+2\s+foo\s+true\s*/;
      if (regex.test(actualCols)) {
        throw "actual:\n" + actualCols + "\ndoes not match regex";
      }
    }
  )";
  ASSERT_TRUE(Eval(test_string));
}
}  // namespace shell
