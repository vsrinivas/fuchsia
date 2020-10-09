// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime.h"

#include <lib/syslog/cpp/macros.h>
#include <string.h>

#include "src/developer/shell/josh/lib/fdio.h"
#include "src/developer/shell/josh/lib/fidl.h"
#include "src/developer/shell/josh/lib/sys.h"
#include "src/developer/shell/josh/lib/zx.h"
#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

// This is present and needs to be called in the most recent version of quickjs.
// It's only here so that we can run against both the old and new version until
// we integrate the new version.
__attribute__((weak)) void js_std_init_handlers(JSRuntime* rt) {}

namespace shell {

Runtime::Runtime() {
  rt_ = JS_NewRuntime();
  js_std_init_handlers(rt_);
  is_valid_ = (rt_ == nullptr);

  // The correct loader for ES6 modules.  Not sure why this has to be done by hand.
  JS_SetModuleLoaderFunc(rt_, nullptr, js_module_loader, nullptr);
}

Runtime::~Runtime() {
  if (is_valid_) {
    js_std_free_handlers(rt_);
    JS_FreeRuntime(rt_);
  }
}

Context::Context(const Runtime* rt) : ctx_(JS_NewContext(rt->Get())) {
  is_valid_ = (ctx_ == nullptr);

#if __has_feature(address_sanitizer)
  // ASan tends to exceed the max stack size of 256K.
  JS_SetMaxStackSize(rt->Get(), 1024 * 1024);
#endif  // __has_feature(address_sanitizer)
}

Context::~Context() {
  if (is_valid_) {
    JS_FreeContext(ctx_);
  }
}

bool Context::Export(const std::string& lib, const std::string& js_path) {
  std::string path;
  int flags = JS_EVAL_TYPE_MODULE;
  if (!js_path.empty()) {
    path = js_path;
    if (path[path.length() - 1] != '/') {
      path.append("/");
    }
    path.append(lib);
    path.append(".js");
  } else {
    path = lib;
    flags |= JS_EVAL_FLAG_COMPILE_ONLY;
  }
  std::string init_str = "import * as " + lib + " from '" + path +
                         "';\n"
                         "globalThis." +
                         lib + " = " + lib + ";\n";

  JSValue init_compile = JS_Eval(ctx_, init_str.c_str(), init_str.length(), "<input>", flags);

  if (JS_IsException(init_compile)) {
    return false;
  }

  if (js_path.empty()) {
    js_module_set_import_meta(ctx_, init_compile, 1, 1);
    JSValue init_run = JS_EvalFunction(ctx_, init_compile);

    if (JS_IsException(init_run)) {
      js_std_dump_error(ctx_);
      return false;
    }
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

bool Context::InitBuiltins(const std::string& fidl_path, const std::string& boot_js_path) {
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

  if (sys::SysModuleInit(ctx_, "sys") == nullptr) {
    return false;
  }
  if (!Export("sys")) {
    return false;
  }

  if (!boot_js_path.empty()) {
    if (!Export("pp", boot_js_path)) {
      return false;
    }
    if (!Export("ns", boot_js_path)) {
      return false;
    }
    if (!Export("task", boot_js_path)) {
      return false;
    }
  }

  return true;
}

}  // namespace shell
