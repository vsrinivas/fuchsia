// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include "src/developer/shell/josh/lib/qjs_util.h"
#include "src/developer/shell/mirror/client.h"
#include "third_party/quickjs/quickjs.h"

#define FLAG(x) JS_PROP_INT32_DEF(#x, x, JS_PROP_CONFIGURABLE)
#define FLAG_64(x) JS_PROP_INT64_DEF(#x, x, JS_PROP_CONFIGURABLE)

const char *global_server = nullptr;

namespace shell::sys {

JSValue Reload(JSContext *ctx, JSValueConst /*this_val*/, int argc, JSValueConst *argv) {
  if (argc != 1) {
    return JS_ThrowSyntaxError(ctx,
                               "Wrong number of arguments to sys.setServer, "
                               "was %d, expected 1",
                               argc);
  }
  CStringHolder host_port(ctx, argv[0]);
  mirror::client::ClientConnection connection;
  mirror::Err e = connection.Init(host_port.get());
  if (e.code != mirror::ErrorType::kNone) {
    return JS_ThrowInternalError(ctx, "Problem reloading: %s", e.msg.c_str());
  }
  mirror::Files files;
  e = connection.Load(&files);
  if (e.code != mirror::ErrorType::kNone) {
    return JS_ThrowInternalError(ctx, "Problem reloading: %s", e.msg.c_str());
  }
  return JS_UNDEFINED;
}

const JSCFunctionListEntry funcs_[] = {JS_CFUNC_DEF("reload", 0, Reload)};

namespace {

int SysRunOnInit(JSContext *ctx, JSModuleDef *m) {
  return JS_SetModuleExportList(ctx, m, funcs_, countof(funcs_));
}

}  // namespace

JSModuleDef *SysModuleInit(JSContext *ctx, const char *module_name) {
  JSModuleDef *m = JS_NewCModule(ctx, module_name, SysRunOnInit);
  if (!m) {
    return nullptr;
  }
  JS_AddModuleExportList(ctx, m, funcs_, countof(funcs_));
  return m;
}

}  // namespace shell::sys
