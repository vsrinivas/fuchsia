// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zircon/process.h>

#include <array>
#include <fstream>
#include <string>

#include <task-utils/walker.h>

#include "gtest/gtest.h"
#include "src/developer/shell/lib/js_testing_utils.h"

namespace shell {

class TaskTest : public JsTest {
 protected:
  void SetUp() override { JsTest::SetUp(); }
};

TEST_F(TaskTest, SimplePs) {
  InitBuiltins("/pkg/data/fidling", "/pkg/data/lib");

  // Loop up-front to populate the svc object, which is done via a promise.
  js_std_loop(ctx_->Get());
  std::string test_string = R"(
      globalThis.resultOne = undefined;
      task.ps().
        then((result) => {
            globalThis.resultOne = result; }).
        catch((e) => { std.printf(e); std.printf(e.stack); globalThis.resultOne = e;});
  )";
  ASSERT_TRUE(Eval(test_string));
  js_std_loop(ctx_->Get());
  test_string = R"(
      let res = globalThis.resultOne;
      if (res instanceof Error) {
        throw res;
      }
      if (res.size <= 0) {
        throw "No tasks found by ps?";
      }
      res.forEach((value, key, map) => {
          if (!key.hasOwnProperty("name") || !key.hasOwnProperty("info")) {
              throw "Missing task information in " + JSON.stringify(key);
          }

       });
  )";
  ASSERT_TRUE(Eval(test_string));
}

}  // namespace shell
