// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <vector>

#include "lib/fidl/txn_header.h"
#include "src/developer/shell/josh/lib/object_converter.h"
#include "src/developer/shell/josh/lib/qjs_util.h"
#include "src/developer/shell/josh/lib/zx.h"
#include "src/lib/fidl_codec/encoder.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/wire_parser.h"
#include "third_party/quickjs/quickjs.h"
// TODO(fxbug.dev/57392): Move it back to //third_party once unification completes.
#include "zircon/third_party/rapidjson/include/rapidjson/stringbuffer.h"
#include "zircon/third_party/rapidjson/include/rapidjson/writer.h"

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
  loader->AddPath(std::string(path), &loader_err);

  return JS_NewBool(ctx, loader_err.value == fidl_codec::LibraryReadError::kOk);
}

// Loads a FIDL library from a string containing its JSON.
// argv[0] A string name of the library (e.g., "fuchsia.io")
// argv[1] A string containing the IR of the library.
// Returns a boolean indicating success.
JSValue LoadLibraryFromString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  if (argc != 2) {
    return JS_ThrowSyntaxError(ctx,
                               "Wrong number of arguments to fidl.loadLibraryFromString(), "
                               "was %d, expected 2",
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
  CStringHolder contents(ctx, argv[1]);
  loader->AddContent(std::string(contents.get()), &loader_err);

  return JS_NewBool(ctx, loader_err.value == fidl_codec::LibraryReadError::kOk);
}

// Returns an object with a "bytes" and "handles" field containing the encoded version of a fidl
// request.
// argv[0] = Transaction ID.
// argv[1] = Ordinal.
// argv[2] = Object.
JSValue EncodeRequest(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  const uint8_t kFidlMagic = 1;
  const uint8_t kFlags[3] = {0, 0, 0};

  if (argc != 3) {
    return JS_ThrowSyntaxError(ctx,
                               "Wrong number of arguments to fidl.encodeRequest(), "
                               "was %d, expected 3",
                               argc);
  }
  auto loader =
      reinterpret_cast<fidl_codec::LibraryLoader*>(JS_GetOpaque(this_val, fidl_class_id_));
  if (loader == nullptr) {
    return JS_EXCEPTION;
  }

  int32_t txn_id_signed;
  if (JS_ToInt32(ctx, &txn_id_signed, argv[0]) == -1) {
    return JS_EXCEPTION;
  }
  auto txn_id = static_cast<uint32_t>(txn_id_signed);

  int64_t ordinal_signed;
  if (JS_ToBigInt64(ctx, &ordinal_signed, argv[1]) == -1) {
    return JS_EXCEPTION;
  }
  auto ordinal = static_cast<fidl_codec::Ordinal64>(ordinal_signed);

  const std::vector<const fidl_codec::InterfaceMethod*>* methods = loader->GetByOrdinal(ordinal);

  if (!methods || methods->empty()) {
    return JS_ThrowInternalError(ctx, "Method not found for ordinal %zu", ordinal);
  }

  auto method = (*methods)[0];
  auto request = method->request();
  if (request == nullptr) {
    return JS_ThrowInternalError(ctx, "Method missing request.");
  }

  auto ast = ObjectConverter::Convert(ctx, request, argv[2]);

  if (!ast || !ast->AsStructValue()) {
    return JS_EXCEPTION;
  }

  auto result = fidl_codec::Encoder::EncodeMessage(txn_id, ordinal, kFlags, kFidlMagic,
                                                   *ast->AsStructValue());

  auto bytes = JS_NewArrayBufferCopy(ctx, result.bytes.data(), result.bytes.size());
  auto handles = JS_NewArray(ctx);
  JS_SetPropertyStr(ctx, handles, "length",
                    JS_NewInt32(ctx, static_cast<int32_t>(result.handles.size())));

  for (uint32_t i = 0; i < result.handles.size(); i++) {
    JSValue opaque_handle = zx::HandleCreate(ctx, result.handles[i].handle, result.handles[i].type);
    JSValue user_handle = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, user_handle, "_handle", opaque_handle);
    JS_SetPropertyUint32(ctx, handles, i, user_handle);
  }

  auto ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, ret, "bytes", bytes);
  JS_SetPropertyStr(ctx, ret, "handles", handles);
  return ret;
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
  std::unique_ptr<fidl_codec::StructValue> object;
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
    JS_CFUNC_DEF("loadLibraryFromString", 1, LoadLibraryFromString),
    JS_CFUNC_DEF("encodeRequest", 1, EncodeRequest),
    JS_CFUNC_DEF("decodeResponse", 1, DecodeResponse),
    JS_CFUNC_DEF("close", 1, Close),
};

JSCFunctionListEntry module_funcs_[] = {JS_CFUNC_DEF("newLibrary", 1, NewLibrary),
                                        JS_PROP_STRING_DEF("irPath", "", JS_PROP_CONFIGURABLE)};

int FidlRunOnInit(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&fidl_class_id_);
  JS_NewClass(JS_GetRuntime(ctx), fidl_class_id_, &fidl_class_);
  JSValue proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, proto, fidl_proto_funcs_, std::size(fidl_proto_funcs_));
  JS_SetClassProto(ctx, fidl_class_id_, proto);
  JS_SetModuleExportList(ctx, m, module_funcs_, std::size(module_funcs_));
  return 0;
};

}  // namespace

JSModuleDef* FidlModuleInit(JSContext* ctx, const char* module_name, const std::string& fidl_path) {
  JSModuleDef* m = JS_NewCModule(ctx, module_name, FidlRunOnInit);
  if (!m) {
    return nullptr;
  }
  module_funcs_[1].u.str = fidl_path.c_str();
  JS_AddModuleExportList(ctx, m, module_funcs_, std::size(module_funcs_));
  return m;
}

}  // namespace shell::fidl
