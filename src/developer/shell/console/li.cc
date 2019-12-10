// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "li.h"

#include <stdio.h>

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
  if (argc != 0) {
    return JS_EXCEPTION;
  }
  repl::Repl *repl = new repl::Repl(ctx, "li > ");
  JSValue obj = JS_NewObjectClass(ctx, js_repl_class_id);
  if (JS_IsException(obj)) {
    return obj;
  }
  JS_SetOpaque(obj, repl);
  return obj;
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

// Expects 3 arguments: a repl::Repl object, a byte buffer and the number of relevant bytes
// in the byte buffer
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
// evaluates repl->cmd_, and returns an array [error_in_script (boolean), script_result]
JSValue GetAndEvalCmd(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc != 1) {
    return JS_EXCEPTION;
  }
  repl::Repl *repl = reinterpret_cast<repl::Repl *>(JS_GetOpaque(argv[0], js_repl_class_id));
  const char *cmd = repl->GetCmd();
  JSValue script_result = JS_Eval(ctx, cmd, strlen(cmd), "<evalScript>", JS_EVAL_TYPE_GLOBAL);
  bool error_in_script = false;
  if (JS_IsException(script_result)) {
    js_std_dump_error(ctx);
    error_in_script = true;
  }
  JSValue result = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, result, 0, JS_NewBool(ctx, error_in_script));
  JS_SetPropertyUint32(ctx, result, 1, script_result);
  return result;
}

// Expects 2 argument: a repl::Repl object and a JS object, the result of a script evaluation
// (that is not an error)
JSValue ShowOutput(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc != 2) {
    return JS_EXCEPTION;
  }
  repl::Repl *repl = reinterpret_cast<repl::Repl *>(JS_GetOpaque(argv[0], js_repl_class_id));
  const char *output = JS_ToCString(ctx, argv[1]);
  repl->Write(output);
  repl->Write("\n");
  return JS_NewBool(ctx, true);
}

// Expects 1 argument: a repl::Repl object
JSValue ShowPrompt(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc != 1) {
    return JS_EXCEPTION;
  }
  repl::Repl *repl = reinterpret_cast<repl::Repl *>(JS_GetOpaque(argv[0], js_repl_class_id));
  repl->ShowPrompt();
  return JS_NewBool(ctx, true);
}

// Expects 1 argument: a repl::Repl object
JSValue GetLine(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc != 1) {
    return JS_EXCEPTION;
  }
  repl::Repl *repl = reinterpret_cast<repl::Repl *>(JS_GetOpaque(argv[0], js_repl_class_id));
  return JS_NewString(ctx, repl->GetLine());
}

const JSCFunctionListEntry js_li_funcs[] = {
    JS_CFUNC_DEF("createRepl", 0, NewRepl),    JS_CFUNC_DEF("onInput", 3, OnInput),
    JS_CFUNC_DEF("closeRepl", 1, CloseRepl),   JS_CFUNC_DEF("getAndEvalCmd", 1, GetAndEvalCmd),
    JS_CFUNC_DEF("showPrompt", 1, ShowPrompt), JS_CFUNC_DEF("showOutput", 2, ShowOutput),
    JS_CFUNC_DEF("getLine", 1, GetLine)};

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

// Expects one argument: the repl as a JSValue
repl::Repl *GetRepl(JSContext *ctx, JSValueConst repl_js) {
  repl::Repl *repl = reinterpret_cast<repl::Repl *>(JS_GetOpaque(repl_js, js_repl_class_id));
  return repl;
}

}  // namespace li

}  // namespace shell
