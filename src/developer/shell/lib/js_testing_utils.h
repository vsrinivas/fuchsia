// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_LIB_JS_TESTING_UTILS_H_
#define SRC_DEVELOPER_SHELL_LIB_JS_TESTING_UTILS_H_

#include <zircon/status.h>
#include <zircon/types.h>

#include "src/developer/shell/lib/runtime.h"
#include "third_party/quickjs/quickjs.h"

namespace shell {

// A class that supports running a test inside a quickjs context.
class JsTest : public ::testing::Test {
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
  }

  // Initializes shell-specific modules, including fidl, zx, and fdio.
  // |fidl_path| points to where you look for FIDL JSON IR.
  // |js_lib_path| points to where you look for system JS libs.
  void InitBuiltins(const std::string& fidl_path, const std::string& js_lib_path) {
    if (!ctx_->InitBuiltins(fidl_path, js_lib_path)) {
      ctx_->DumpError();
      FAIL();
    }
  }

  bool Eval(std::string command) {
    JSValue result = JS_Eval(ctx_->Get(), command.c_str(), command.length(), "batch", 0);
    if (JS_IsException(result)) {
      ctx_->DumpError();
      return false;
    }
    return true;
  }

  std::unique_ptr<Context> ctx_;
  std::unique_ptr<Runtime> rt_;
};

}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_LIB_JS_TESTING_UTILS_H_
