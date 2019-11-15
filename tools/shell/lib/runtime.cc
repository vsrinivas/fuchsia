// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime.h"

#include <string.h>

#include "src/lib/fxl/logging.h"
#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"
#include "tools/shell/lib/fdio.h"
#include "tools/shell/lib/fidl.h"
#include "tools/shell/lib/zx.h"

namespace shell {

Runtime::Runtime() {
  rt_ = JS_NewRuntime();
  is_valid_ = (rt_ == nullptr);
}

Runtime::~Runtime() {
  if (is_valid_) {
    JS_FreeRuntime(rt_);
  }
}

Context::Context(const Runtime* rt) : ctx_(JS_NewContext(rt->Get())) {
  is_valid_ = (ctx_ == nullptr);
}

Context::~Context() {
  if (is_valid_) {
    JS_FreeContext(ctx_);
  }
}

bool Context::Export(const std::string& lib) {
  std::string init_str = "import * as " + lib + " from '" + lib +
                         "';\n"
                         "globalThis." +
                         lib + " = " + lib + ";\n";
  JSValue init_compile = JS_Eval(ctx_, init_str.c_str(), init_str.length(), "<input>",
                                 JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

  if (JS_IsException(init_compile)) {
    return false;
  }

  js_module_set_import_meta(ctx_, init_compile, 1, 1);
  JSValue init_run = JS_EvalFunction(ctx_, init_compile);

  if (JS_IsException(init_run)) {
    return false;
  }

  return true;
}

bool Context::InitStd() {
  // System modules
  js_init_module_std(ctx_, "std");
  if (!Export("std")) {
    return false;
  }

  js_init_module_os(ctx_, "os");
  if (!Export("os")) {
    return false;
  }

  return true;
}

extern "C" const uint8_t qjsc_fidl[];
extern "C" const uint32_t qjsc_fidl_size;
extern "C" const uint8_t qjsc_fdio[];
extern "C" const uint32_t qjsc_fdio_size;
extern "C" const uint8_t qjsc_zx[];
extern "C" const uint32_t qjsc_zx_size;

bool Context::InitBuiltins(const std::string& fidl_path) {
  if (fdio::FdioModuleInit(ctx_, "fdio") == nullptr) {
    return false;
  }
  if (!Export("fdio")) {
    return false;
  }

  if (fidl::FidlModuleInit(ctx_, "fidl_internal", fidl_path) == nullptr) {
    return false;
  }
  js_std_eval_binary(ctx_, qjsc_fidl, qjsc_fidl_size, 0);

  if (zx::ZxModuleInit(ctx_, "zx_internal") == nullptr) {
    return false;
  }
  js_std_eval_binary(ctx_, qjsc_zx, qjsc_zx_size, 0);
  return true;
}

}  // namespace shell
