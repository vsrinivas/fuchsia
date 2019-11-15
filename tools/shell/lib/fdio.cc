// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fdio.h"

#include <lib/fdio/directory.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "third_party/quickjs/quickjs.h"
#include "tools/shell/lib/qjs_util.h"
#include "tools/shell/lib/zx.h"

namespace shell::fdio {

namespace {

// Wrapper around fdio_service_connect.
// argv[0] is a (string) path to the service.
// Returns a handle object that points to the client endpoint to this service.
JSValue ServiceConnect(JSContext *ctx, JSValueConst /*this_val*/, int argc, JSValueConst *argv) {
  if (argc != 1) {
    return JS_ThrowSyntaxError(ctx,
                               "Wrong number of arguments to fdio.serviceConnect(), "
                               "was %d, expected 1",
                               argc);
  }
  zx_handle_t out0, out1;
  zx_status_t status;

  status = zx_channel_create(0, &out0, &out1);
  if (status != ZX_OK) {
    return zx::ZxStatusToError(ctx, status);
  }

  CStringHolder path(ctx, argv[0]);
  if (!path.get()) {
    return JS_EXCEPTION;
  }

  status = fdio_service_connect(path.get(), out1);

  if (status != ZX_OK) {
    zx_handle_close(out0);
    zx_handle_close(out1);
    return zx::ZxStatusToError(ctx, status);
  }
  return zx::HandleCreate(ctx, out0, ZX_OBJ_TYPE_CHANNEL);
}

const JSCFunctionListEntry funcs_[] = {
    JS_CFUNC_DEF("serviceConnect", 1, ServiceConnect),
};

int FdioRunOnInit(JSContext *ctx, JSModuleDef *m) {
  return JS_SetModuleExportList(ctx, m, funcs_, countof(funcs_));
}

}  // namespace

// Returns a module that supports FDIO functionality.
JSModuleDef *FdioModuleInit(JSContext *ctx, const char *module_name) {
  JSModuleDef *m = JS_NewCModule(ctx, module_name, FdioRunOnInit);
  if (!m) {
    return nullptr;
  }
  JS_AddModuleExportList(ctx, m, funcs_, countof(funcs_));
  return m;
}
}  // namespace shell::fdio
