// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <vector>

#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/wire_parser.h"
#include "third_party/quickjs/quickjs.h"
#include "third_party/rapidjson/include/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/include/rapidjson/writer.h"
#include "tools/shell/lib/qjs_util.h"
#include "tools/shell/lib/zx.h"

namespace shell::fidl {

// TODO(jeremymanson): Dedup from fidl_codec.
namespace {

std::string DocumentToString(rapidjson::Document* document) {
  rapidjson::StringBuffer output;
  rapidjson::Writer<rapidjson::StringBuffer> writer(output);
  document->Accept(writer);
  return output.GetString();
}

JSClassID fidl_class_id_;

JSClassDef fidl_class_ = {
    "FidlInternal",
    .finalizer = nullptr,
};

// Loads a FIDL library.
// argv[0] A string name of the library (e.g., "fuchsia.io")
// Returns a boolean indicating success.
JSValue LoadLibrary(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  if (argc != 1) {
    return JS_ThrowSyntaxError(ctx,
                               "Wrong number of arguments to fidl.loadLibrary(), "
                               "was %d, expected 1",
                               argc);
  }
  auto loader =
      reinterpret_cast<fidl_codec::LibraryLoader*>(JS_GetOpaque(this_val, fidl_class_id_));
  if (loader == nullptr) {
    return JS_EXCEPTION;
  }

  CStringHolder val(ctx, argv[0]);
  const char* path = val.get();
  if (!path) {
    return JS_EXCEPTION;
  }
  fidl_codec::LibraryReadError loader_err;
  std::unique_ptr<std::istream> path_ptr(new std::ifstream(path));
  loader->Add(&path_ptr, &loader_err);

  return JS_NewBool(ctx, loader_err.value == fidl_codec::LibraryReadError::kOk);
}

// Returns a string with the JSON representation of this FIDL message.
// argv[0] = bytes
// argv[1] = handles
JSValue DecodeResponse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  if (argc != 2) {
    return JS_ThrowSyntaxError(ctx,
                               "Wrong number of arguments to fidl.decodeResponse(), "
                               "was %d, expected 2",
                               argc);
  }
  auto loader =
      reinterpret_cast<fidl_codec::LibraryLoader*>(JS_GetOpaque(this_val, fidl_class_id_));
  if (loader == nullptr) {
    return JS_EXCEPTION;
  }

  size_t message_len;
  uint8_t* message_buf = JS_GetArrayBuffer(ctx, &message_len, argv[0]);
  if (message_buf == nullptr || message_len < sizeof(fidl_message_header_t)) {
    return JS_NewString(ctx, "");
  }

  if (!JS_IsArray(ctx, argv[1])) {
    return JS_ThrowSyntaxError(ctx, "Expected array of handles");
  }
  int32_t handles_len;
  // It's an array, so assume this works...
  JS_ToInt32(ctx, &handles_len, JS_GetPropertyStr(ctx, argv[1], "length"));
  std::array<zx_handle_info_t, ZX_CHANNEL_MAX_MSG_HANDLES> handle_buf;
  // Check if handles returned anything useful.
  for (int32_t i = 0; i < handles_len; i++) {
    JSValue val = JS_GetPropertyUint32(ctx, argv[1], i);
    handle_buf[i] = zx::HandleFromJsval(val);
  }

  auto header = reinterpret_cast<const fidl_message_header_t*>(message_buf);
  const std::vector<const fidl_codec::InterfaceMethod*>* methods =
      loader->GetByOrdinal(header->ordinal);
  // Test method not found, but...
  const fidl_codec::InterfaceMethod* method = (*methods)[0];
  std::unique_ptr<fidl_codec::Object> object;
  std::ostringstream errors;
  if (!fidl_codec::DecodeResponse(method, message_buf, message_len, handle_buf.data(), handles_len,
                                  &object, errors)) {
    return JS_ThrowTypeError(ctx, "%s", errors.str().c_str());
  }
  rapidjson::Document actual_response;
  if (object != nullptr) {
    object->ExtractJson(actual_response.GetAllocator(), actual_response);
    std::string val = DocumentToString(&actual_response);
    return JS_NewString(ctx, val.c_str());
  }

  return JS_NewString(ctx, "");
}

// Returns a new library object, which hides a fidl_codec::LibraryLoader.
JSValue NewLibrary(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* /*argv*/) {
  if (argc != 0) {
    return JS_ThrowSyntaxError(ctx,
                               "Wrong number of arguments to fidl.decodeResponse(), "
                               "was %d, expected 0",
                               argc);
  }
  JSValue new_library = JS_NewObjectClass(ctx, fidl_class_id_);
  if (JS_IsException(new_library)) {
    return JS_EXCEPTION;
  }
  JS_SetOpaque(new_library, new fidl_codec::LibraryLoader());
  return new_library;
}

// Closes the library passed in via this_val.
JSValue Close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* /*argv*/) {
  if (argc != 0) {
    return JS_ThrowSyntaxError(ctx,
                               "Wrong number of arguments to fidl.decodeResponse(), "
                               "was %d, expected 0",
                               argc);
  }
  auto loader =
      reinterpret_cast<fidl_codec::LibraryLoader*>(JS_GetOpaque(this_val, fidl_class_id_));
  if (loader == nullptr) {
    return JS_EXCEPTION;
  }
  delete loader;
  JS_SetOpaque(this_val, nullptr);
  return JS_UNDEFINED;
}

const JSCFunctionListEntry fidl_proto_funcs_[] = {
    JS_CFUNC_DEF("loadLibrary", 1, LoadLibrary),
    JS_CFUNC_DEF("decodeResponse", 1, DecodeResponse),
    JS_CFUNC_DEF("close", 1, Close),
};

JSCFunctionListEntry module_funcs_[] = {JS_CFUNC_DEF("newLibrary", 1, NewLibrary),
                                        JS_PROP_STRING_DEF("irPath", "", JS_PROP_CONFIGURABLE)};

int FidlRunOnInit(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&fidl_class_id_);
  JS_NewClass(JS_GetRuntime(ctx), fidl_class_id_, &fidl_class_);
  JSValue proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, proto, fidl_proto_funcs_, countof(fidl_proto_funcs_));
  JS_SetClassProto(ctx, fidl_class_id_, proto);
  JS_SetModuleExportList(ctx, m, module_funcs_, countof(module_funcs_));
  return 0;
};

}  // namespace

JSModuleDef* FidlModuleInit(JSContext* ctx, const char* module_name, const std::string& fidl_path) {
  JSModuleDef* m = JS_NewCModule(ctx, module_name, FidlRunOnInit);
  if (!m) {
    return nullptr;
  }
  module_funcs_[1].u.str = fidl_path.c_str();
  JS_AddModuleExportList(ctx, m, module_funcs_, countof(module_funcs_));
  return m;
}

}  // namespace shell::fidl
