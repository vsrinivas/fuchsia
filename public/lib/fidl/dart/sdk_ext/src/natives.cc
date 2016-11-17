// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/dart/sdk_ext/src/natives.h"

#include <magenta/syscalls.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <vector>

#include "dart/runtime/include/dart_api.h"
#include "lib/fidl/dart/sdk_ext/src/handle_watcher.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace fidl {
namespace dart {
namespace {

constexpr int kNumberOfNativeFields = 2;

struct HandlePeer {
  Dart_WeakPersistentHandle weak;
  mx_handle_t handle;
};

}  // namespace

#define REGISTER_FUNCTION(name, count) {"" #name, name, count},
#define DECLARE_FUNCTION(name, count) \
  extern void name(Dart_NativeArguments args);

#define FIDL_NATIVE_LIST(V)          \
  V(MxChannel_Create, 1)             \
  V(MxChannel_Write, 5)              \
  V(MxChannel_Read, 5)               \
  V(MxChannel_QueryAndRead, 3)       \
  V(MxTime_Get, 1)                   \
  V(MxHandle_Close, 1)               \
  V(MxHandle_RegisterFinalizer, 2)   \
  V(MxHandle_UnregisterFinalizer, 1) \
  V(MxHandleWatcher_SendControlData, 5)

FIDL_NATIVE_LIST(DECLARE_FUNCTION);

static struct NativeEntries {
  const char* name;
  Dart_NativeFunction function;
  int argument_count;
} Entries[] = {FIDL_NATIVE_LIST(REGISTER_FUNCTION)};

Dart_NativeFunction NativeLookup(Dart_Handle name,
                                 int argument_count,
                                 bool* auto_setup_scope) {
  const char* function_name = nullptr;
  Dart_Handle result = Dart_StringToCString(name, &function_name);
  DART_CHECK_VALID(result);
  assert(function_name != nullptr);
  assert(auto_setup_scope != nullptr);
  *auto_setup_scope = true;
  size_t num_entries = arraysize(Entries);
  for (size_t i = 0; i < num_entries; ++i) {
    const struct NativeEntries& entry = Entries[i];
    if (!strcmp(function_name, entry.name) &&
        (entry.argument_count == argument_count)) {
      return entry.function;
    }
  }
  return nullptr;
}

const uint8_t* NativeSymbol(Dart_NativeFunction nf) {
  size_t num_entries = arraysize(Entries);
  for (size_t i = 0; i < num_entries; ++i) {
    const struct NativeEntries& entry = Entries[i];
    if (entry.function == nf) {
      return reinterpret_cast<const uint8_t*>(entry.name);
    }
  }
  return nullptr;
}

static void SetNullReturn(Dart_NativeArguments arguments) {
  Dart_SetReturnValue(arguments, Dart_Null());
}

static void SetInvalidArgumentReturn(Dart_NativeArguments arguments) {
  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(ERR_INVALID_ARGS));
}

#define CHECK_INTEGER_ARGUMENT(args, num, result, failure)       \
  {                                                              \
    Dart_Handle __status;                                        \
    __status = Dart_GetNativeIntegerArgument(args, num, result); \
    if (Dart_IsError(__status)) {                                \
      Set##failure##Return(arguments);                           \
      return;                                                    \
    }                                                            \
  }

static void HandleFinalizer(void* isolate_data,
                            Dart_WeakPersistentHandle weak,
                            void* peer_ptr) {
  HandlePeer* peer = reinterpret_cast<HandlePeer*>(peer_ptr);
  if (peer->handle != MX_HANDLE_INVALID)
    mx_handle_close(peer->handle);
  delete peer;
}

void MxHandle_RegisterFinalizer(Dart_NativeArguments arguments) {
  Dart_Handle wrapper = Dart_GetNativeArgument(arguments, 0);

  intptr_t native_fields[kNumberOfNativeFields];
  FTL_CHECK(!Dart_IsError(Dart_GetNativeFieldsOfArgument(
      arguments, 0, kNumberOfNativeFields, native_fields)));
  FTL_CHECK(!native_fields[0]);
  FTL_CHECK(!native_fields[1]);

  int64_t raw_handle = static_cast<int64_t>(MX_HANDLE_INVALID);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &raw_handle, InvalidArgument);
  if (raw_handle == static_cast<int64_t>(MX_HANDLE_INVALID)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  mx_handle_t handle = static_cast<mx_handle_t>(raw_handle);
  HandlePeer* peer = new HandlePeer();

  FTL_CHECK(!Dart_IsError(Dart_SetNativeInstanceField(
      wrapper, 0, reinterpret_cast<intptr_t>(peer))));

  peer->handle = handle;
  peer->weak = Dart_NewWeakPersistentHandle(wrapper, peer, sizeof(HandlePeer),
                                            HandleFinalizer);
  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(NO_ERROR));
}

void MxHandle_UnregisterFinalizer(Dart_NativeArguments arguments) {
  Dart_Handle wrapper = Dart_GetNativeArgument(arguments, 0);

  intptr_t native_fields[kNumberOfNativeFields];
  FTL_CHECK(!Dart_IsError(Dart_GetNativeFieldsOfArgument(
      arguments, 0, kNumberOfNativeFields, native_fields)));
  FTL_CHECK(native_fields[0]);
  FTL_CHECK(!native_fields[1]);

  HandlePeer* peer = reinterpret_cast<HandlePeer*>(native_fields[0]);
  FTL_CHECK(!Dart_IsError(Dart_SetNativeInstanceField(wrapper, 0, 0)));

  Dart_DeleteWeakPersistentHandle(Dart_CurrentIsolate(), peer->weak);
  delete peer;

  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(NO_ERROR));
}

void MxTime_Get(Dart_NativeArguments arguments) {
  int64_t clock_id;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &clock_id, InvalidArgument);

  mx_time_t time = mx_time_get(clock_id);
  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(time));
}

void MxHandle_Close(Dart_NativeArguments arguments) {
  int64_t dart_handle;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &dart_handle, InvalidArgument);

  mx_handle_t handle = static_cast<mx_handle_t>(dart_handle);
  mx_status_t rv = mx_handle_close(handle);

  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(rv));
}

void MxChannel_Create(Dart_NativeArguments arguments) {
  int64_t options = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &options, Null);

  mx_handle_t end1 = MX_HANDLE_INVALID;
  mx_handle_t end2 = MX_HANDLE_INVALID;
  mx_status_t rv = mx_channel_create(options, &end1, &end2);

  Dart_Handle list = Dart_NewList(3);
  Dart_ListSetAt(list, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(list, 1, Dart_NewInteger(end1));
  Dart_ListSetAt(list, 2, Dart_NewInteger(end2));
  Dart_SetReturnValue(arguments, list);
}

void MxChannel_Write(Dart_NativeArguments arguments) {
  int64_t handle = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &handle, InvalidArgument);

  Dart_Handle typed_data = Dart_GetNativeArgument(arguments, 1);
  if (!Dart_IsTypedData(typed_data) && !Dart_IsNull(typed_data)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t num_bytes = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 2, &num_bytes, InvalidArgument);
  if ((Dart_IsNull(typed_data) && (num_bytes != 0)) ||
      (!Dart_IsNull(typed_data) && (num_bytes <= 0))) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  Dart_Handle dart_handles = Dart_GetNativeArgument(arguments, 3);
  if (!Dart_IsList(dart_handles) && !Dart_IsNull(dart_handles)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t options = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 4, &options, InvalidArgument);

  // Grab the data if there is any.
  Dart_TypedData_Type typ;
  void* bytes = nullptr;
  intptr_t bdlen = 0;
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataAcquireData(typed_data, &typ, &bytes, &bdlen);
  }

  // Grab the handles if there are any.
  std::vector<mx_handle_t> handles;
  intptr_t handles_len = 0;
  if (!Dart_IsNull(dart_handles)) {
    Dart_ListLength(dart_handles, &handles_len);
    if (handles_len > 0) {
      handles.resize(handles_len);
    }
    for (int i = 0; i < handles_len; i++) {
      Dart_Handle dart_handle = Dart_ListGetAt(dart_handles, i);
      if (!Dart_IsInteger(dart_handle)) {
        if (!Dart_IsNull(typed_data)) {
          Dart_TypedDataReleaseData(typed_data);
        }
        SetInvalidArgumentReturn(arguments);
        return;
      }
      int64_t handle = 0;
      Dart_IntegerToInt64(dart_handle, &handle);
      handles[i] = static_cast<mx_handle_t>(handle);
    }
  }

  mx_status_t rv = mx_channel_write(
      static_cast<mx_handle_t>(handle), options, const_cast<const void*>(bytes),
      static_cast<uint32_t>(num_bytes), handles.data(), handles.size());

  // Release the data.
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataReleaseData(typed_data);
  }

  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(rv));
}

void MxChannel_Read(Dart_NativeArguments arguments) {
  int64_t handle = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &handle, Null);

  Dart_Handle typed_data = Dart_GetNativeArgument(arguments, 1);
  if (!Dart_IsTypedData(typed_data) && !Dart_IsNull(typed_data)) {
    SetNullReturn(arguments);
    return;
  }
  // When querying the amount of data available to read from the pipe,
  // null is passed in for typed_data.

  int64_t num_bytes = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 2, &num_bytes, Null);
  if ((Dart_IsNull(typed_data) && (num_bytes != 0)) ||
      (!Dart_IsNull(typed_data) && (num_bytes <= 0))) {
    SetNullReturn(arguments);
    return;
  }

  Dart_Handle dart_handles = Dart_GetNativeArgument(arguments, 3);
  if (!Dart_IsList(dart_handles) && !Dart_IsNull(dart_handles)) {
    SetNullReturn(arguments);
    return;
  }

  int64_t options = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 4, &options, Null);

  // Grab the data if there is any.
  Dart_TypedData_Type typ;
  void* bytes = nullptr;
  intptr_t byte_data_len = 0;
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataAcquireData(typed_data, &typ, &bytes, &byte_data_len);
  }
  uint32_t blen = static_cast<uint32_t>(num_bytes);

  // Grab the handles if there are any.
  std::vector<mx_handle_t> handles;
  intptr_t handles_len = 0;
  if (!Dart_IsNull(dart_handles)) {
    Dart_ListLength(dart_handles, &handles_len);
    handles.resize(handles_len);
  }
  uint32_t hlen = static_cast<uint32_t>(handles_len);

  mx_status_t rv =
      mx_channel_read(static_cast<mx_handle_t>(handle), options, bytes, blen,
                      &blen, handles.data(), hlen, &hlen);

  // Release the data.
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataReleaseData(typed_data);
  }

  if (!Dart_IsNull(dart_handles)) {
    for (int i = 0; i < handles_len; i++) {
      Dart_ListSetAt(dart_handles, i, Dart_NewInteger(handles[i]));
    }
  }

  Dart_Handle list = Dart_NewList(3);
  Dart_ListSetAt(list, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(list, 1, Dart_NewInteger(blen));
  Dart_ListSetAt(list, 2, Dart_NewInteger(hlen));
  Dart_SetReturnValue(arguments, list);
}

void ByteArrayFinalizer(void* isolate_callback_data,
                        Dart_WeakPersistentHandle handle,
                        void* peer) {
  uint8_t* byte_array = reinterpret_cast<uint8_t*>(peer);
  delete[] byte_array;
}

void HandleArrayFinalizer(void* isolate_callback_data,
                          Dart_WeakPersistentHandle handle,
                          void* peer) {
  uint32_t* handle_array = reinterpret_cast<uint32_t*>(peer);
  delete[] handle_array;
}

void MxChannel_QueryAndRead(Dart_NativeArguments arguments) {
  Dart_Handle err;
  int64_t dart_handle;
  int64_t options = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &dart_handle, Null);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &options, Null);
  Dart_Handle result = Dart_GetNativeArgument(arguments, 2);

  Dart_Handle data = Dart_ListGetAt(result, 1);
  Dart_Handle dart_handles = Dart_ListGetAt(result, 2);

  // Query the number of bytes and dart_handles available.
  uint32_t blen = 0;
  uint32_t hlen = 0;
  mx_status_t rv = mx_channel_read(static_cast<mx_handle_t>(dart_handle), 0,
                                   nullptr, 0, &blen, nullptr, 0, &hlen);

  if ((rv != NO_ERROR) && (rv != ERR_BUFFER_TOO_SMALL)) {
    Dart_ListSetAt(result, 0, Dart_NewInteger(rv));
    Dart_ListSetAt(result, 1, data);
    Dart_ListSetAt(result, 2, dart_handles);
    Dart_ListSetAt(result, 3, Dart_NewInteger(0));
    Dart_ListSetAt(result, 4, Dart_NewInteger(0));
    return;
  }

  Dart_TypedData_Type typ;
  void* bytes = nullptr;
  intptr_t bytes_len = 0;
  if ((blen > 0) && Dart_IsNull(data)) {
    uint8_t* new_byte_data = new uint8_t[blen];
    data = Dart_NewExternalTypedData(Dart_TypedData_kByteData, new_byte_data,
                                     blen);
    FTL_DCHECK(!Dart_IsError(data));
    Dart_NewWeakPersistentHandle(data, new_byte_data, blen, ByteArrayFinalizer);
  } else if (blen > 0) {
    err = Dart_TypedDataAcquireData(data, &typ, &bytes, &bytes_len);
    FTL_DCHECK(!Dart_IsError(err));
    err = Dart_TypedDataReleaseData(data);
    FTL_DCHECK(!Dart_IsError(err));
    if (static_cast<uintptr_t>(bytes_len) < blen) {
      uint8_t* new_byte_data = new uint8_t[blen];
      data = Dart_NewExternalTypedData(Dart_TypedData_kByteData, new_byte_data,
                                       blen);
      FTL_DCHECK(!Dart_IsError(data));
      Dart_NewWeakPersistentHandle(data, new_byte_data, blen,
                                   ByteArrayFinalizer);
    }
  }

  void* handle_bytes = nullptr;
  intptr_t handles_len = 0;
  if ((hlen > 0) && Dart_IsNull(dart_handles)) {
    uint32_t* new_handle_data = new uint32_t[hlen];
    dart_handles = Dart_NewExternalTypedData(Dart_TypedData_kUint32,
                                             new_handle_data, hlen);
    FTL_DCHECK(!Dart_IsError(dart_handles));
    Dart_NewWeakPersistentHandle(dart_handles, new_handle_data,
                                 hlen * sizeof(uint32_t), HandleArrayFinalizer);
  } else if (hlen > 0) {
    err = Dart_TypedDataAcquireData(dart_handles, &typ, &handle_bytes,
                                    &handles_len);
    FTL_DCHECK(!Dart_IsError(err));
    err = Dart_TypedDataReleaseData(dart_handles);
    FTL_DCHECK(!Dart_IsError(err));
    if (static_cast<uintptr_t>(handles_len) < hlen) {
      uint32_t* new_handle_data = new uint32_t[hlen];
      dart_handles = Dart_NewExternalTypedData(Dart_TypedData_kUint32,
                                               new_handle_data, hlen);
      FTL_DCHECK(!Dart_IsError(dart_handles));
      Dart_NewWeakPersistentHandle(dart_handles, new_handle_data,
                                   hlen * sizeof(uint32_t),
                                   HandleArrayFinalizer);
    }
  }

  if (blen > 0) {
    err = Dart_TypedDataAcquireData(data, &typ, &bytes, &bytes_len);
    FTL_DCHECK(!Dart_IsError(err));
  }

  if (hlen > 0) {
    err = Dart_TypedDataAcquireData(dart_handles, &typ, &handle_bytes,
                                    &handles_len);
    FTL_DCHECK(!Dart_IsError(err));
  }

  rv = mx_channel_read(
      static_cast<mx_handle_t>(dart_handle), options, bytes, blen, &blen,
      reinterpret_cast<mx_handle_t*>(handle_bytes), hlen, &hlen);

  if (blen > 0) {
    err = Dart_TypedDataReleaseData(data);
    FTL_DCHECK(!Dart_IsError(err));
  }

  if (hlen > 0) {
    err = Dart_TypedDataReleaseData(dart_handles);
    FTL_DCHECK(!Dart_IsError(err));
  }

  Dart_ListSetAt(result, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(result, 1, data);
  Dart_ListSetAt(result, 2, dart_handles);
  Dart_ListSetAt(result, 3, Dart_NewInteger(blen));
  Dart_ListSetAt(result, 4, Dart_NewInteger(hlen));
}

void MxHandleWatcher_SendControlData(Dart_NativeArguments arguments) {
  int64_t control_handle = 0;
  int64_t command_code;
  int64_t handle_or_deadline = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &control_handle, InvalidArgument);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &command_code, InvalidArgument);
  CHECK_INTEGER_ARGUMENT(arguments, 2, &handle_or_deadline, InvalidArgument);

  Dart_Handle send_port_handle = Dart_GetNativeArgument(arguments, 3);
  Dart_Port send_port_id = ILLEGAL_PORT;
  if (!Dart_IsNull(send_port_handle)) {
    Dart_Handle result = Dart_SendPortGetId(send_port_handle, &send_port_id);
    if (Dart_IsError(result)) {
      SetInvalidArgumentReturn(arguments);
      return;
    }
  }

  int64_t signals = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 4, &signals, InvalidArgument);

  HandleWatcherCommand command = HandleWatcherCommand::FromDart(
      command_code, handle_or_deadline, send_port_id, signals);
  mx_status_t rv = mx_channel_write(control_handle, 0,
                                    reinterpret_cast<const void*>(&command),
                                    sizeof(command), nullptr, 0);
  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(rv));
}

}  // namespace dart
}  // namespace fidl
