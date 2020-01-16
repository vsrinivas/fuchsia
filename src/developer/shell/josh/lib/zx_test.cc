// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/josh/lib/zx.h"

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "src/developer/shell/josh/lib/js_testing_utils.h"
#include "src/developer/shell/josh/lib/runtime.h"

namespace shell {

class ZxTest : public JsTest {
 protected:
  void SetUp() override { JsTest::SetUp(); }
};

// Sanity check test to make sure Hello World works.
TEST_F(ZxTest, BasicChannelOps) {
  InitBuiltins("", "");
  std::string test_string = R"(
const TEST_VAL = 42;
let ch = zx.Channel.create();
let p = new Promise((resolve, reject) => {
  ch[1].wait(zx.ZX_CHANNEL_READABLE | zx.ZX_CHANNEL_PEER_CLOSED, () => {
    arr = ch[1].read();
    if (arr[0][0] != TEST_VAL) {
      reject("Did not read correct test value: " + JSON.stringify(arr));
    }
    resolve();
  })
});
let writeBuffer = new ArrayBuffer(1);
let view = new DataView(writeBuffer);
view.setInt8(0, TEST_VAL);
ch[0].write(writeBuffer, []);
Promise.all([p]);
)";
  ASSERT_TRUE(Eval(test_string));
}

}  // namespace shell
