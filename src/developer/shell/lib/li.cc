// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "li.h"

#include "repl.h"
#include "src/lib/line_input/line_input.h"
#include "third_party/quickjs/list.h"

namespace shell {

namespace {

JSClassID js_repl_class_id;

JSClassDef js_repl_class = {
    "Repl",
    .finalizer = nullptr,
};

// Expects no arguments
JSValue NewRepl(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  repl::Repl *repl = new repl::Repl(ctx, "li > ");
  JSValue obj = JS_NewObjectClass(ctx, js_repl_class_id);
  if (JS_IsException(obj)) {
    return obj;
  }
  JS_SetOpaque(obj, repl);
  return obj;
}

// Expects 3 arguments: a repl::Repl object, a byte buffer and
// the number of relevant bytes in the byte buffer
JSValue OnInput(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc != 3) {
    return JS_EXCEPTION;
  }
  repl::Repl *repl = reinterpret_cast<repl::Repl *>(JS_GetOpaque(argv[0], js_repl_class_id));
  size_t num_bytes;
  uint8_t *bytes = JS_GetArrayBuffer(ctx, &num_bytes, argv[1]);
  if (!bytes) {
    return JS_ThrowTypeError(ctx, "Expected an ArrayBuffer");
  }
  int len;
  if (JS_ToInt32(ctx, &len, argv[2])) {
    return JS_EXCEPTION;
  }
  bool exit_shell = repl->FeedInput(bytes, len);
  return JS_NewBool(ctx, exit_shell);
}

// Expects 1 argument: a repl::Repl object
JSValue CloseRepl(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc != 1) {
    return JS_EXCEPTION;
  }
  repl::Repl *repl = reinterpret_cast<repl::Repl *>(JS_GetOpaque(argv[0], js_repl_class_id));
  delete repl;
  return JS_NewBool(ctx, true);
}

const JSCFunctionListEntry js_li_funcs[] = {JS_CFUNC_DEF("createRepl", 1, NewRepl),
                                            JS_CFUNC_DEF("onInput", 3, OnInput),
                                            JS_CFUNC_DEF("closeRepl", 1, CloseRepl)};

int LiRunOnInit(JSContext *ctx, JSModuleDef *m) {
  /* Repl Input class */
  JS_NewClassID(&js_repl_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_repl_class_id, &js_repl_class);
  return JS_SetModuleExportList(ctx, m, js_li_funcs, countof(js_li_funcs));
};

}  // namespace

namespace li {
JSModuleDef *LiModuleInit(JSContext *ctx, const char *module_name) {
  JSModuleDef *m = JS_NewCModule(ctx, module_name, LiRunOnInit);
  if (!m) {
    return nullptr;
  }
  JS_AddModuleExportList(ctx, m, js_li_funcs, countof(js_li_funcs));
  return m;
}
}  // namespace li

}  // namespace shell
