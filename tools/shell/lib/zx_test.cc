// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/shell/lib/zx.h"

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "tools/shell/lib/runtime.h"

namespace shell {

class ZxTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rt_ = std::make_unique<Runtime>();
    ASSERT_NE(nullptr, rt_.get()) << "Cannot allocate JS runtime";

    ctx_ = std::make_unique<Context>(rt_.get());
    ASSERT_NE(nullptr, ctx_.get()) << "Cannot allocate JS context";
    if (!ctx_->InitStd()) {
      ctx_->DumpError();
      FAIL();
    }

    if (!ctx_->InitBuiltins()) {
      ctx_->DumpError();
      FAIL();
    }
  }

  void Eval(std::string command) {
    JSValue result = JS_Eval(ctx_->Get(), command.c_str(), command.length(), "batch", 0);
    if (JS_IsException(result)) {
      ctx_->DumpError();
      FAIL();
    }
  }

  std::unique_ptr<Context> ctx_;
  std::unique_ptr<Runtime> rt_;
};

// Sanity check test to make sure Hello World works.
TEST_F(ZxTest, BasicChannelOps) {
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
  Eval(test_string);
}

}  // namespace shell
