// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zx.h"

#include <zircon/syscalls.h>

#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

// This file contains bindings that allow JavaScript code to invoke syscalls.

namespace shell {

namespace zx {

// Converts a zx_status_t into a JavaScript error and throws it.
JSValue ZxStatusToError(JSContext *ctx, zx_status_t status) {
  if (status == ZX_OK) {
    return JS_UNDEFINED;
  }
  JSValue obj = JS_NewError(ctx);
  JS_DefinePropertyValueStr(ctx, obj, "message", JS_NewString(ctx, zx_status_get_string(status)),
                            JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  JS_DefinePropertyValueStr(ctx, obj, "status", JS_NewInt32(ctx, status),
                            JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  return JS_Throw(ctx, obj);
}

JSClassID handle_class_id_;

JSClassDef handle_class_ = {
    "Handle",
    .finalizer = nullptr,
};

zx_handle_info_t HandleFromJsval(JSValue val) {
  JSFuchsiaHandle *opaque =
      reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque(val, handle_class_id_));
  zx_handle_info_t handle;
  handle.handle = opaque->handle;
  handle.type = opaque->type;
  return handle;
}

JSValue HandleCreate(JSContext *ctx, zx_handle_t handle, zx_obj_type_t type) {
  JSValue obj = JS_NewObjectClass(ctx, handle_class_id_);
  if (JS_IsException(obj)) {
    return obj;
  }
  JSFuchsiaHandle *s =
      reinterpret_cast<JSFuchsiaHandle *>(js_mallocz(ctx, sizeof(JSFuchsiaHandle)));
  if (!s) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }
  s->handle = handle;
  s->type = type;
  JS_SetOpaque(obj, s);
  return obj;
}

JSValue HandleClose(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc != 1) {
    return JS_ThrowSyntaxError(ctx, "Wrong number of arguments to zx.close(), was %d, expected 1",
                               argc);
  }
  JSFuchsiaHandle *h =
      reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, argv[0], handle_class_id_));
  if (!h) {
    return JS_EXCEPTION;
  }
  stop_waiting_for_zx_handle(JS_GetRuntime(ctx), h, -1);
  zx_handle_close(h->handle);
  h->handle = ZX_HANDLE_INVALID;
  return JS_UNDEFINED;
}

JSValue ObjectWaitAsync(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc != 3) {
    return JS_ThrowSyntaxError(ctx, "Wrong number of arguments to zx.close(), was %d, expected 3",
                               argc);
  }

  JSFuchsiaHandle *h =
      reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, argv[0], handle_class_id_));
  if (!h) {
    return JS_EXCEPTION;
  }

  uint32_t signals;
  if (JS_ToUint32(ctx, &signals, argv[1])) {
    return JS_EXCEPTION;
  }

  if (!JS_IsFunction(ctx, argv[2])) {
    return JS_ThrowTypeError(ctx, "Expected a function");
  }

  if (zx_object_get_info(h->handle, ZX_INFO_HANDLE_VALID, NULL, 0, NULL, NULL) ==
      ZX_ERR_BAD_HANDLE) {
    return JS_ThrowTypeError(ctx, "Invalid handle");
  }

  wait_for_zx_handle(ctx, h, signals, &argv[2]);

  return JS_UNDEFINED;
}

JSValue ChannelCreate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  zx_handle_t out0, out1;
  zx_status_t status = zx_channel_create(0, &out0, &out1);
  if (status != ZX_OK) {
    return ZxStatusToError(ctx, status);
  }

  JSValue handles = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, handles, 0, HandleCreate(ctx, out0, ZX_OBJ_TYPE_CHANNEL));
  JS_SetPropertyUint32(ctx, handles, 1, HandleCreate(ctx, out1, ZX_OBJ_TYPE_CHANNEL));
  return handles;
}

// This gets passed a Handle object.
// TODO(jeremymanson): Support flags
JSValue ChannelRead(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc != 1) {
    return JS_ThrowSyntaxError(
        ctx, "Wrong number of arguments to zx.channelRead(), was %d, expected 1", argc);
  }
  JSFuchsiaHandle *h =
      reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, argv[0], handle_class_id_));
  if (!h) {
    return JS_EXCEPTION;
  }

  uint32_t num_bytes;
  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  uint32_t num_handles;
  zx_handle_info_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];

  zx_status_t status = zx_channel_read_etc(h->handle, 0, bytes, handles, countof(bytes),
                                           countof(handles), &num_bytes, &num_handles);
  if (status != ZX_OK) {
    return ZxStatusToError(ctx, status);
  }

  JSValue bytes_buffer = JS_NewArrayBufferCopy(ctx, bytes, num_bytes);
  JSValue handles_array = JS_NewArray(ctx);
  for (uint32_t i = 0; i < num_handles; i++) {
    JS_SetPropertyUint32(ctx, handles_array, i,
                         HandleCreate(ctx, handles[i].handle, handles[i].type));
  }

  // TODO(jeremymanson): We can do better than an array here.
  JSValue ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, bytes_buffer);
  JS_SetPropertyUint32(ctx, ret, 1, handles_array);

  return ret;
}

// Takes a Handle, an array of bytes, and an array of Handles.
// TODO(jeremymanson): Should this be an array of zx.Objects?
JSValue ChannelWrite(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc != 3) {
    return JS_ThrowSyntaxError(ctx, "Wrong number of arguments to zx.write(), was %d, expected 3",
                               argc);
  }

  JSFuchsiaHandle *h =
      reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, argv[0], handle_class_id_));
  if (!h) {
    return JS_EXCEPTION;
  }

  size_t num_bytes;
  uint8_t *bytes = JS_GetArrayBuffer(ctx, &num_bytes, argv[1]);
  if (!bytes) {
    return JS_ThrowTypeError(ctx, "Expected an ArrayBuffer");
  }
  if (num_bytes > ZX_CHANNEL_MAX_MSG_BYTES) {
    return JS_ThrowRangeError(ctx, "Message length exceeds %d bytes", ZX_CHANNEL_MAX_MSG_BYTES);
  }

  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t num_handles;
  if (!JS_IsArray(ctx, argv[2])) {
    return JS_ThrowTypeError(ctx, "Expected an Array");
  }
  JSValue num_handles_value = JS_GetPropertyStr(ctx, argv[2], "length");
  if (JS_IsException(num_handles_value)) {
    return num_handles_value;
  }
  if (JS_ToUint32(ctx, &num_handles, num_handles_value)) {
    return JS_EXCEPTION;
  }
  if (num_handles > ZX_CHANNEL_MAX_MSG_HANDLES) {
    return JS_ThrowRangeError(ctx, "Message handle count exceeds %d", ZX_CHANNEL_MAX_MSG_HANDLES);
  }
  for (uint32_t i = 0; i < num_handles; i++) {
    JSValue item = JS_GetPropertyUint32(ctx, argv[2], i);
    JSFuchsiaHandle *ih =
        reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, item, handle_class_id_));
    if (!ih) {
      return JS_ThrowTypeError(ctx, "Expected a Handle at index %d", i);
    }
    handles[i] = ih->handle;
    ih->handle = ZX_HANDLE_INVALID;
  }

  zx_status_t status = zx_channel_write(h->handle, 0, bytes, num_bytes, handles, num_handles);

  return ZxStatusToError(ctx, status);
}

JSValue Duplicate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc != 2) {
    return JS_ThrowSyntaxError(ctx, "Wrong number of arguments to zx.write(), was %d, expected 2",
                               argc);
  }
  JSFuchsiaHandle *h =
      reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, argv[0], handle_class_id_));
  if (!h) {
    return JS_EXCEPTION;
  }
  uint32_t right_mask;
  if (JS_ToUint32(ctx, &right_mask, argv[1])) {
    return JS_EXCEPTION;
  }
  zx_handle_t out;
  zx_status_t status = zx_handle_duplicate(h->handle, right_mask, &out);
  if (status != ZX_OK) {
    return ZxStatusToError(ctx, status);
  }
  return HandleCreate(ctx, out, h->type);
}

#define FLAG(x) JS_PROP_INT32_DEF(#x, x, JS_PROP_CONFIGURABLE)
#define FLAG_64(x) JS_PROP_INT64_DEF(#x, x, JS_PROP_CONFIGURABLE)

const JSCFunctionListEntry funcs_[] = {
    /* Fuchsia handle operations */
    JS_CFUNC_DEF("channelCreate", 0, ChannelCreate), JS_CFUNC_DEF("channelRead", 0, ChannelRead),
    JS_CFUNC_DEF("channelWrite", 0, ChannelWrite), JS_CFUNC_DEF("handleClose", 0, HandleClose),
    JS_CFUNC_DEF("objectWaitAsync", 0, ObjectWaitAsync),
    /* Handle signal constants */
    FLAG(ZX_CHANNEL_READABLE), FLAG(ZX_CHANNEL_WRITABLE), FLAG(ZX_CHANNEL_PEER_CLOSED),
    /* zx_object_get_info flags */
    FLAG(ZX_INFO_NONE), FLAG(ZX_INFO_HANDLE_VALID), FLAG(ZX_INFO_HANDLE_BASIC),
    FLAG(ZX_INFO_PROCESS), FLAG(ZX_INFO_PROCESS_THREADS), FLAG(ZX_INFO_VMAR),
    FLAG(ZX_INFO_JOB_CHILDREN), FLAG(ZX_INFO_JOB_PROCESSES), FLAG(ZX_INFO_THREAD),
    FLAG(ZX_INFO_THREAD_EXCEPTION_REPORT), FLAG(ZX_INFO_TASK_STATS), FLAG(ZX_INFO_PROCESS_MAPS),
    FLAG(ZX_INFO_PROCESS_VMOS), FLAG(ZX_INFO_THREAD_STATS), FLAG(ZX_INFO_CPU_STATS),
    FLAG(ZX_INFO_KMEM_STATS), FLAG(ZX_INFO_RESOURCE), FLAG(ZX_INFO_HANDLE_COUNT), FLAG(ZX_INFO_BTI),
    FLAG(ZX_INFO_PROCESS_HANDLE_STATS), FLAG(ZX_INFO_SOCKET), FLAG(ZX_INFO_VMO),
    FLAG_64(ZX_RIGHT_SAME_RIGHTS)};

namespace {

int ZxRunOnInit(JSContext *ctx, JSModuleDef *m) {
  JS_NewClassID(&handle_class_id_);
  JS_NewClass(JS_GetRuntime(ctx), handle_class_id_, &handle_class_);
  return JS_SetModuleExportList(ctx, m, funcs_, countof(funcs_));
}

}  // namespace

JSModuleDef *ZxModuleInit(JSContext *ctx, const char *module_name) {
  JSModuleDef *m = JS_NewCModule(ctx, module_name, ZxRunOnInit);
  if (!m) {
    return nullptr;
  }
  JS_AddModuleExportList(ctx, m, funcs_, countof(funcs_));
  return m;
}

}  // namespace zx

}  // namespace shell
