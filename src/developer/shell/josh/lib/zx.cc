// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zx.h"

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <iostream>
#include <iterator>
#include <memory>
#include <string>

#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

// This file contains bindings that allow JavaScript code to invoke syscalls.

namespace shell::zx {

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
  auto opaque = reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque(val, handle_class_id_));
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
  auto s = reinterpret_cast<JSFuchsiaHandle *>(js_mallocz(ctx, sizeof(JSFuchsiaHandle)));
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
  auto h = reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, argv[0], handle_class_id_));
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

  auto h = reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, argv[0], handle_class_id_));
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
  auto h = reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, argv[0], handle_class_id_));
  if (!h) {
    return JS_EXCEPTION;
  }

  uint32_t num_bytes;
  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  uint32_t num_handles;
  zx_handle_info_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];

  zx_status_t status = zx_channel_read_etc(h->handle, 0, bytes, handles, std::size(bytes),
                                           std::size(handles), &num_bytes, &num_handles);
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

  auto h = reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, argv[0], handle_class_id_));
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
    auto ih = reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, item, handle_class_id_));
    if (!ih) {
      return JS_ThrowTypeError(ctx, "Expected a handle at index %d into handle array", i);
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
  auto h = reinterpret_cast<JSFuchsiaHandle *>(JS_GetOpaque2(ctx, argv[0], handle_class_id_));
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

// Converts a number to a Handle object, where number is a zx_handle_t that the code
// got from somewhere.
// argv[0] is the Number.
static JSValue HandleFromInt(JSContext *ctx, JSValueConst /*this_val*/, int argc,
                             JSValueConst *argv) {
  if (argc != 1) {
    return JS_EXCEPTION;
  }

  zx_handle_t handle;
  if (JS_ToInt32(ctx, reinterpret_cast<int32_t *>(&handle), argv[0])) {
    return JS_EXCEPTION;
  }

  zx_info_handle_basic_t basic;
  if (zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &basic, sizeof(basic), nullptr, nullptr) !=
      ZX_OK) {
    return JS_ThrowTypeError(ctx, "Invalid handle");
  }
  return HandleCreate(ctx, handle, basic.type);
}

// Calls zx_object_get_child.
// argv[0] is the handle
// argv[1] is the koid for which you want the handle.
// argv[2] is the rights you have on the child handle.
// Returns the handle of the child.
JSValue GetChild(JSContext *ctx, JSValueConst /*this_val*/, int argc, JSValueConst *argv) {
  if (argc != 3) {
    return JS_ThrowSyntaxError(ctx, "Bad arguments to zx.getChild()");
  }
  static_assert(sizeof(zx_rights_t) == sizeof(uint32_t));
  uint64_t koid;
  zx_rights_t rights;
  zx_handle_info_t handle_info = HandleFromJsval(argv[0]);
  JS_ToInt64(ctx, reinterpret_cast<int64_t *>(&koid), argv[1]);
  JS_ToInt32(ctx, reinterpret_cast<int32_t *>(&rights), argv[2]);
  zx_handle_t out;
  zx_status_t status = zx_object_get_child(handle_info.handle, koid, rights, &out);
  if (status != ZX_OK) {
    return ZxStatusToError(ctx, status);
  }
  zx_info_handle_basic_t basic;
  if (zx_object_get_info(out, ZX_INFO_HANDLE_BASIC, &basic, sizeof(basic), nullptr, nullptr) !=
      ZX_OK) {
    return JS_ThrowTypeError(ctx, "Invalid handle");
  }
  return HandleCreate(ctx, out, basic.type);
}

// Provides a generic interface for dealing with the output of zx_object_get_info, that
// can be specialized for the kind of info we're getting.
class GetInfoController {
 public:
  GetInfoController(JSContext *ctx, uint32_t topic) : ctx_(ctx), topic_(topic) {}
  virtual ~GetInfoController() = default;

  // Creates a buffer for the output of zx_object_get_info.  |size| is a hint, and may be
  // ignored if you know better.  However, if it isn't enough room, we'll loop until it is.
  virtual void *SetBuffer(size_t size) = 0;

  // Gets the current buffer size.
  virtual size_t BufferSize() = 0;

  // Gets a reference to the buffer.
  virtual void *GetBuffer() = 0;

  // Converts the contents of the buffer into a JSValue (usually with the same fields as the struct)
  virtual JSValue GetValues(size_t actual) = 0;

  // Gets the controller for the given |topic|, where topic is the same as the topic of the
  // zx_object_get_info.
  static GetInfoController *GetCorrectController(JSContext *ctx, uint32_t topic);

 protected:
  JSContext *ctx_;
  uint32_t topic_;
};

// GetInfoController for zx_koid_t[]
class KoidInfoController : public GetInfoController {
 public:
  KoidInfoController(JSContext *ctx, uint32_t topic) : GetInfoController(ctx, topic) {}
  void *SetBuffer(size_t size) override {
    auto *arr = new zx_koid_t[size];
    buffer_.reset(arr);
    size_ = size;
    return reinterpret_cast<void *>(arr);
  }
  size_t BufferSize() override { return size_; }
  void *GetBuffer() override { return buffer_.get(); }
  JSValue GetValues(size_t actual) override {
    JSValue arr = JS_NewArray(ctx_);
    for (size_t i = 0; i < actual; i++) {
      JS_SetPropertyUint32(ctx_, arr, i, JS_NewInt64(ctx_, buffer_.get()[i]));
    }
    return arr;
  }

 private:
  std::unique_ptr<zx_koid_t[]> buffer_;
  size_t size_;
};

// GetInfoController for zx_info_handle_basic_t.
class BasicInfoController : public GetInfoController {
 public:
  BasicInfoController(JSContext *ctx, uint32_t topic) : GetInfoController(ctx, topic) {}
  size_t BufferSize() override { return sizeof(basic_info_); }
  void *GetBuffer() override { return &basic_info_; }
  void *SetBuffer(size_t /*size*/) override { return GetBuffer(); }
  JSValue GetValues(size_t /*actual*/) override {
    JSValue object = JS_NewObject(ctx_);

    if (JS_SetPropertyStr(ctx_, object, "koid", JS_NewInt32(ctx_, basic_info_.koid)) != 1) {
      return JS_ThrowInternalError(ctx_, "Unable to set koid");
    }
    if (JS_SetPropertyStr(ctx_, object, "rights", JS_NewInt32(ctx_, basic_info_.rights)) != 1) {
      return JS_ThrowInternalError(ctx_, "Unable to set rights");
    }
    if (JS_SetPropertyStr(ctx_, object, "type", JS_NewInt32(ctx_, basic_info_.type)) != 1) {
      return JS_ThrowInternalError(ctx_, "Unable to set type");
    }
    if (JS_SetPropertyStr(ctx_, object, "related_koid",
                          JS_NewInt32(ctx_, basic_info_.related_koid)) != 1) {
      return JS_ThrowInternalError(ctx_, "Unable to set related_koid");
    }
    return object;
  }

 private:
  zx_info_handle_basic_t basic_info_;
};

GetInfoController *GetInfoController::GetCorrectController(JSContext *ctx, uint32_t topic) {
  switch (topic) {
    case ZX_INFO_JOB_CHILDREN:
    case ZX_INFO_JOB_PROCESSES:
    case ZX_INFO_PROCESS_THREADS:
      return new KoidInfoController(ctx, topic);
    case ZX_INFO_HANDLE_VALID:
      return nullptr;
    case ZX_INFO_HANDLE_BASIC:
      return new BasicInfoController(ctx, topic);
    case ZX_INFO_PROCESS:
    case ZX_INFO_VMAR:
    case ZX_INFO_THREAD:
    case ZX_INFO_THREAD_EXCEPTION_REPORT:
    case ZX_INFO_TASK_STATS:
    case ZX_INFO_PROCESS_MAPS:
    case ZX_INFO_PROCESS_VMOS:
    case ZX_INFO_THREAD_STATS:
    case ZX_INFO_CPU_STATS:
    case ZX_INFO_KMEM_STATS:
    case ZX_INFO_RESOURCE:
    case ZX_INFO_HANDLE_COUNT:
    case ZX_INFO_BTI:
    case ZX_INFO_PROCESS_HANDLE_STATS:
    case ZX_INFO_SOCKET:
    case ZX_INFO_VMO:
    default:
      return nullptr;
  }
}

// Calls zx_object_get_info, and returns a JSValue that looks like the struct returned
// by that call.
// argv[0] is a handle, argv[1] is the topic
JSValue ObjectGetInfo(JSContext *ctx, JSValueConst /*this_val*/, int argc, JSValueConst *argv) {
  if (argc != 2) {
    return JS_ThrowSyntaxError(ctx, "Bad arguments to zx.objectGetInfo");
  }
  zx_handle_info_t handle_info = HandleFromJsval(argv[0]);
  uint32_t topic;
  if (JS_ToUint32(ctx, &topic, argv[1])) {
    return JS_ThrowSyntaxError(ctx, "Bad topic for zx.objectGetInfo");
  }
  GetInfoController *c = GetInfoController::GetCorrectController(ctx, topic);
  if (c == nullptr) {
    std::string msg = "zx.objectGetInfo topic " + std::to_string(topic) + " not implemented";
    return JS_ThrowSyntaxError(ctx, "%s", msg.c_str());
  }
  std::unique_ptr<GetInfoController> controller;
  controller.reset(c);
  std::unique_ptr<void *> buffer;
  size_t actual;
  size_t avail;
  constexpr size_t kMaxAttempts = 5;
  size_t attempt = 0;
  // 16 seems like a nice round number.
  size_t buffer_size = 16;
  do {
    attempt++;
    void *buffer = controller->SetBuffer(buffer_size);
    zx_status_t status = zx_object_get_info(handle_info.handle, topic, buffer,
                                            controller->BufferSize(), &actual, &avail);
    if (status != ZX_OK) {
      return ZxStatusToError(ctx, status);
    }
    buffer_size *= 2;
  } while (actual < avail && attempt < kMaxAttempts);
  return controller->GetValues(actual);
}

// argv[0] is a handle
// argv[1] is a legal property for zx_object_get_property.
JSValue ObjectGetProperty(JSContext *ctx, JSValueConst /*this_val*/, int argc, JSValueConst *argv) {
  if (argc != 2) {
    return JS_ThrowSyntaxError(ctx, "Bad arguments to zx.objectGetProperty");
  }
  zx_handle_info_t handle_info = HandleFromJsval(argv[0]);
  uint32_t property;
  if (JS_ToUint32(ctx, &property, argv[1])) {
    return JS_ThrowSyntaxError(ctx, "Bad property for zx.objectGetProperty");
  }
  if (property != ZX_PROP_NAME) {
    return JS_ThrowInternalError(ctx, "Operation %d not supported on zx.objectGetProperty",
                                 property);
  }
  char name[ZX_MAX_NAME_LEN];
  zx_status_t status = zx_object_get_property(handle_info.handle, property, name, sizeof(name));
  if (status != ZX_OK) {
    return ZxStatusToError(ctx, status);
  }
  return JS_NewString(ctx, name);
}

JSValue ProcessSelf(JSContext *ctx, JSValueConst /*this_val*/, int /*argc*/,
                    JSValueConst * /*argv*/) {
  zx_handle_t self = zx_process_self();
  return HandleCreate(ctx, self, ZX_OBJ_TYPE_PROCESS);
}

JSValue JobDefault(JSContext *ctx, JSValueConst /*this_val*/, int /*argc*/,
                   JSValueConst * /*argv*/) {
  zx_handle_t d = zx_job_default();
  return HandleCreate(ctx, d, ZX_OBJ_TYPE_JOB);
}

// Calls zx_task_kill
// argv[0] is a handle to the task to kill
JSValue Kill(JSContext *ctx, JSValueConst /*this_val*/, int argc, JSValueConst *argv) {
  if (argc != 1) {
    return JS_ThrowSyntaxError(ctx, "Bad arguments to zx.kill");
  }
  zx_handle_info_t handle_info = HandleFromJsval(argv[0]);

  zx_status_t status = zx_task_kill(handle_info.handle);
  if (status != ZX_OK) {
    return ZxStatusToError(ctx, status);
  }
  return JS_NewInt32(ctx, 0);
}

#define FLAG(x) JS_PROP_INT32_DEF(#x, x, JS_PROP_CONFIGURABLE)
#define FLAG_64(x) JS_PROP_INT64_DEF(#x, x, JS_PROP_CONFIGURABLE)

const JSCFunctionListEntry funcs_[] = {
    /* Fuchsia handle operations */
    JS_CFUNC_DEF("channelCreate", 0, ChannelCreate), JS_CFUNC_DEF("channelRead", 0, ChannelRead),
    JS_CFUNC_DEF("channelWrite", 0, ChannelWrite), JS_CFUNC_DEF("handleClose", 0, HandleClose),
    JS_CFUNC_DEF("objectWaitAsync", 0, ObjectWaitAsync), JS_CFUNC_DEF("duplicate", 0, Duplicate),
    JS_CFUNC_DEF("getChild", 0, GetChild), JS_CFUNC_DEF("handleFromInt", 0, HandleFromInt),
    JS_CFUNC_DEF("getObjectInfo", 2, ObjectGetInfo),
    JS_CFUNC_DEF("getObjectProperty", 2, ObjectGetProperty),
    JS_CFUNC_DEF("jobDefault", 2, JobDefault), JS_CFUNC_DEF("processSelf", 2, ProcessSelf),
    JS_CFUNC_DEF("kill", 1, Kill),
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
    /* zx_object_get_property flags */
    FLAG(ZX_PROP_NAME), FLAG_64(ZX_RIGHT_SAME_RIGHTS)};

namespace {

int ZxRunOnInit(JSContext *ctx, JSModuleDef *m) {
  JS_NewClassID(&handle_class_id_);
  JS_NewClass(JS_GetRuntime(ctx), handle_class_id_, &handle_class_);
  return JS_SetModuleExportList(ctx, m, funcs_, std::size(funcs_));
}

}  // namespace

JSModuleDef *ZxModuleInit(JSContext *ctx, const char *module_name) {
  JSModuleDef *m = JS_NewCModule(ctx, module_name, ZxRunOnInit);
  if (!m) {
    return nullptr;
  }
  JS_AddModuleExportList(ctx, m, funcs_, std::size(funcs_));
  return m;
}

}  // namespace shell::zx
