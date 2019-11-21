// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fdio.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "src/developer/shell/lib/qjs_util.h"
#include "src/developer/shell/lib/zx.h"
#include "third_party/quickjs/quickjs.h"

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

// Makes the root handles (representing elements of the namespace) available to JS callers.

JSClassDef flat_ns_class_ = {
    "FlatNs",
    .finalizer = nullptr,
};

JSClassID flat_ns_class_id_;

// Returns an object that represents the root namespace.
JSValue NsExportRoot(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  fdio_flat_namespace_t *root_ns = nullptr;
  zx_status_t status = fdio_ns_export_root(&root_ns);
  if (status != ZX_OK) {
    return zx::ZxStatusToError(ctx, status);
  }
  JSValue flat_ns_obj = JS_NewObjectClass(ctx, flat_ns_class_id_);
  JS_SetOpaque(flat_ns_obj, reinterpret_cast<void *>(root_ns));
  return flat_ns_obj;
}

// Closes the elements in the root namespace associated with this object.
JSValue NsClose(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  fdio_flat_namespace_t *ns =
      reinterpret_cast<fdio_flat_namespace_t *>(JS_GetOpaque(this_val, flat_ns_class_id_));
  fdio_ns_free_flat_ns(ns);
  return JS_NewInt32(ctx, 0);
}

// Gets the number of handles in the root namespace associated with this object.
JSValue NsGetCount(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  fdio_flat_namespace_t *ns =
      reinterpret_cast<fdio_flat_namespace_t *>(JS_GetOpaque(this_val, flat_ns_class_id_));
  return JS_NewInt32(ctx, ns->count);
}

// Gets a list of Handle objects that refer to the root namespace.  They have
// a handle property and a property representing the type, which is an int
// defined by the PA_HND macro in processargs.h.
JSValue NsGetElements(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  fdio_flat_namespace_t *ns =
      reinterpret_cast<fdio_flat_namespace_t *>(JS_GetOpaque(this_val, flat_ns_class_id_));
  if (ns == nullptr) {
    return JS_EXCEPTION;
  }
  JSValue dirents = JS_NewObject(ctx);
  if (JS_IsException(dirents)) {
    return dirents;
  }
  for (size_t i = 0; i < ns->count; i++) {
    JSValue val = JS_NewObject(ctx);
    if (JS_IsException(val)) {
      return val;
    }
    if (zx_object_get_info(ns->handle[i], ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr) ==
        ZX_ERR_BAD_HANDLE) {
      continue;
    }
    if (JS_SetPropertyStr(ctx, val, "handle",
                          zx::HandleCreate(ctx, ns->handle[i], ZX_OBJ_TYPE_NONE)) < 0) {
      return JS_EXCEPTION;
    }
    if (JS_SetPropertyStr(ctx, val, "type", JS_NewInt32(ctx, ns->type[i])) < 0) {
      return JS_EXCEPTION;
    }
    if (JS_SetPropertyStr(ctx, dirents, ns->path[i], val) < 0) {
      return JS_EXCEPTION;
    }
  }
  return dirents;
}

const JSCFunctionListEntry flat_ns_proto_funcs_[] = {
    JS_CFUNC_DEF("getCount", 0, NsGetCount),
    JS_CFUNC_DEF("getElements", 0, NsGetElements),
    JS_CFUNC_DEF("close", 0, NsClose),
};

const JSCFunctionListEntry funcs_[] = {
    JS_CFUNC_DEF("serviceConnect", 1, ServiceConnect),
    JS_CFUNC_DEF("nsExportRoot", 0, NsExportRoot),
};

int FdioRunOnInit(JSContext *ctx, JSModuleDef *m) {
  JS_NewClassID(&flat_ns_class_id_);
  JS_NewClass(JS_GetRuntime(ctx), flat_ns_class_id_, &flat_ns_class_);
  JSValue proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, proto, flat_ns_proto_funcs_, countof(flat_ns_proto_funcs_));
  JS_SetClassProto(ctx, flat_ns_class_id_, proto);

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
