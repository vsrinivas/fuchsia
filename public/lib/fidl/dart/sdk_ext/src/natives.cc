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
#include "lib/fidl/dart/sdk_ext/src/handle_waiter.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/tonic/dart_library_natives.h"
#include "lib/tonic/handle_table.h"

namespace fidl {
namespace dart {
namespace {

static tonic::DartLibraryNatives* g_natives;

tonic::DartLibraryNatives* InitNatives() {
  tonic::DartLibraryNatives* natives = new tonic::DartLibraryNatives();
  HandleWaiter::RegisterNatives(natives);
  return natives;
}

constexpr int kNumberOfNativeFields = 2;

struct HandlePeer {
  Dart_WeakPersistentHandle weak;
  mx_handle_t handle;
};

}  // namespace

#define REGISTER_FUNCTION(name, count) {"" #name, name, count},
#define DECLARE_FUNCTION(name, count) \
  extern void name(Dart_NativeArguments args);

#define FIDL_NATIVE_LIST(V)        \
  V(MxChannel_Create, 1)           \
  V(MxChannel_Write, 5)            \
  V(MxChannel_Read, 5)             \
  V(MxChannel_QueryAndRead, 3)     \
  V(MxEventpair_Create, 1)         \
  V(MxSocket_Create, 1)            \
  V(MxSocket_Write, 5)             \
  V(MxSocket_Read, 5)              \
  V(MxVmo_Create, 2)               \
  V(MxVmo_GetSize, 1)              \
  V(MxVmo_SetSize, 2)              \
  V(MxVmo_Write, 5)                \
  V(MxVmo_Read, 5)                 \
  V(MxTime_Get, 1)                 \
  V(MxHandle_Close, 1)             \
  V(MxHandle_RegisterFinalizer, 2) \
  V(MxHandle_UnregisterFinalizer, 1)

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
  if (!g_natives)
    g_natives = InitNatives();
  return g_natives->GetNativeFunction(name, argument_count, auto_setup_scope);
}

const uint8_t* NativeSymbol(Dart_NativeFunction native_function) {
  size_t num_entries = arraysize(Entries);
  for (size_t i = 0; i < num_entries; ++i) {
    const struct NativeEntries& entry = Entries[i];
    if (entry.function == native_function) {
      return reinterpret_cast<const uint8_t*>(entry.name);
    }
  }
  if (!g_natives)
    g_natives = InitNatives();
  return g_natives->GetSymbol(native_function);
}

static void SetNullReturn(Dart_NativeArguments arguments) {
  Dart_SetReturnValue(arguments, Dart_Null());
}

static void SetInvalidArgumentReturn(Dart_NativeArguments arguments) {
  Dart_SetIntegerReturnValue(arguments,
                             static_cast<int64_t>(MX_ERR_INVALID_ARGS));
}

#define CHECK_INTEGER_ARGUMENT(num, result, failure)                  \
  {                                                                   \
    Dart_Handle __status;                                             \
    __status = Dart_GetNativeIntegerArgument(arguments, num, result); \
    if (Dart_IsError(__status)) {                                     \
      Set##failure##Return(arguments);                                \
      return;                                                         \
    }                                                                 \
  }

#define CHECK_HANDLE_ARGUMENT(num, result, failure)                            \
  {                                                                            \
    Dart_Handle __arg = Dart_GetNativeArgument(arguments, num);                \
    if (Dart_IsError(__arg)) {                                                 \
      FTL_LOG(WARNING) << "GetNativeArgumentFailed: " << Dart_GetError(__arg); \
      Set##failure##Return(arguments);                                         \
      return;                                                                  \
    }                                                                          \
    Dart_Handle __err = Dart_Null();                                           \
    *result = handle_table.Unwrap(__arg, &__err);                              \
    if (Dart_IsError(__err)) {                                                 \
      Dart_SetReturnValue(arguments, __err);                                   \
      return;                                                                  \
    }                                                                          \
    if (*result == MX_HANDLE_INVALID) {                                        \
      Set##failure##Return(arguments);                                         \
      return;                                                                  \
    }                                                                          \
  }

static void HandleFinalizer(void* isolate_data,
                            Dart_WeakPersistentHandle weak,
                            void* peer_ptr) {
  // TODO(US-324): Close the handle here, while coordinating with the
  // HandleWaiter finalizer to avoid races if necessary.
  HandlePeer* peer = reinterpret_cast<HandlePeer*>(peer_ptr);
  delete peer;
}

void MxHandle_RegisterFinalizer(Dart_NativeArguments arguments) {
  Dart_Handle wrapper = Dart_GetNativeArgument(arguments, 0);

  intptr_t native_fields[kNumberOfNativeFields];
  FTL_CHECK(!Dart_IsError(Dart_GetNativeFieldsOfArgument(
      arguments, 0, kNumberOfNativeFields, native_fields)));
  FTL_CHECK(!native_fields[0]);
  FTL_CHECK(!native_fields[1]);

  mx_handle_t handle = MX_HANDLE_INVALID;
  tonic::HandleTable& handle_table = tonic::HandleTable::Current();
  CHECK_HANDLE_ARGUMENT(1, &handle, InvalidArgument);
  if (handle == MX_HANDLE_INVALID) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  HandlePeer* peer = new HandlePeer();

  FTL_CHECK(!Dart_IsError(Dart_SetNativeInstanceField(
      wrapper, 0, reinterpret_cast<intptr_t>(peer))));

  peer->handle = handle;
  peer->weak = Dart_NewWeakPersistentHandle(wrapper, peer, sizeof(HandlePeer),
                                            HandleFinalizer);
  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(MX_OK));
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

  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(MX_OK));
}

void MxTime_Get(Dart_NativeArguments arguments) {
  int64_t clock_id;
  CHECK_INTEGER_ARGUMENT(0, &clock_id, InvalidArgument);

  mx_time_t time = mx_time_get(clock_id);
  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(time));
}

void MxHandle_Close(Dart_NativeArguments arguments) {
  mx_handle_t handle = MX_HANDLE_INVALID;
  tonic::HandleTable& handle_table = tonic::HandleTable::Current();
  CHECK_HANDLE_ARGUMENT(0, &handle, InvalidArgument);

  mx_status_t rv = handle_table.Close(handle);

  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(rv));
}

void MxChannel_Create(Dart_NativeArguments arguments) {
  int64_t options = 0;
  CHECK_INTEGER_ARGUMENT(0, &options, Null);

  mx_handle_t end1 = MX_HANDLE_INVALID;
  mx_handle_t end2 = MX_HANDLE_INVALID;
  mx_status_t rv = mx_channel_create(options, &end1, &end2);

  tonic::HandleTable& handle_table = tonic::HandleTable::Current();

  Dart_Handle list = Dart_NewList(3);
  Dart_ListSetAt(list, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(list, 1, handle_table.AddAndWrap(end1));
  Dart_ListSetAt(list, 2, handle_table.AddAndWrap(end2));
  Dart_SetReturnValue(arguments, list);
}

void MxChannel_Write(Dart_NativeArguments arguments) {
  mx_handle_t handle = MX_HANDLE_INVALID;
  tonic::HandleTable& handle_table = tonic::HandleTable::Current();
  CHECK_HANDLE_ARGUMENT(0, &handle, InvalidArgument);

  Dart_Handle typed_data = Dart_GetNativeArgument(arguments, 1);
  if (!Dart_IsTypedData(typed_data) && !Dart_IsNull(typed_data)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t num_bytes = 0;
  CHECK_INTEGER_ARGUMENT(2, &num_bytes, InvalidArgument);
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
  CHECK_INTEGER_ARGUMENT(4, &options, InvalidArgument);

  // Grab the data if there is any.
  Dart_TypedData_Type typ;
  void* bytes = nullptr;
  intptr_t bdlen = 0;
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataAcquireData(typed_data, &typ, &bytes, &bdlen);
  }

  // Grab the handles if there are any.
  std::vector<mx_handle_t> handles;
  Dart_Handle error = Dart_Null();
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
      handles[i] = handle_table.Unwrap(dart_handle, &error);
      if (Dart_IsError(error)) {
        if (!Dart_IsNull(typed_data)) {
          Dart_TypedDataReleaseData(typed_data);
        }
        Dart_SetReturnValue(arguments, error);
        return;
      }
    }
  }

  mx_status_t rv = mx_channel_write(
      handle, options, const_cast<const void*>(bytes),
      static_cast<uint32_t>(num_bytes), handles.data(), handles.size());

  if (rv == MX_OK) {
    for (int i = 0; i < handles_len; i++) {
      handle_table.Remove(handles[i]);
    }
  }

  // Release the data.
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataReleaseData(typed_data);
  }

  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(rv));
}

void MxChannel_Read(Dart_NativeArguments arguments) {
  mx_handle_t handle = MX_HANDLE_INVALID;
  tonic::HandleTable& handle_table = tonic::HandleTable::Current();
  CHECK_HANDLE_ARGUMENT(0, &handle, Null);

  Dart_Handle typed_data = Dart_GetNativeArgument(arguments, 1);
  if (!Dart_IsTypedData(typed_data) && !Dart_IsNull(typed_data)) {
    SetNullReturn(arguments);
    return;
  }
  // When querying the amount of data available to read from the pipe,
  // null is passed in for typed_data.

  int64_t num_bytes = 0;
  CHECK_INTEGER_ARGUMENT(2, &num_bytes, Null);
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
  CHECK_INTEGER_ARGUMENT(4, &options, Null);

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

  mx_status_t rv = mx_channel_read(handle, options, bytes, handles.data(), blen,
                                   hlen, &blen, &hlen);

  // Release the data.
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataReleaseData(typed_data);
  }

  if (!Dart_IsNull(dart_handles)) {
    for (int i = 0; i < handles_len; i++) {
      Dart_ListSetAt(dart_handles, i,
                     handle_table.AddAndWrap(handles[i]));
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

void MxChannel_QueryAndRead(Dart_NativeArguments arguments) {
  Dart_Handle err;
  mx_handle_t handle;
  int64_t options = 0;
  tonic::HandleTable& handle_table = tonic::HandleTable::Current();
  CHECK_HANDLE_ARGUMENT(0, &handle, Null);
  CHECK_INTEGER_ARGUMENT(1, &options, Null);
  Dart_Handle result = Dart_GetNativeArgument(arguments, 2);

  Dart_Handle data = Dart_ListGetAt(result, 1);
  Dart_Handle dart_handles = Dart_ListGetAt(result, 2);

  // Query the number of bytes and handles available.
  uint32_t blen = 0;
  uint32_t hlen = 0;
  mx_status_t rv =
      mx_channel_read(handle, 0, nullptr, nullptr, 0, 0, &blen, &hlen);

  if ((rv != MX_OK) && (rv != MX_ERR_BUFFER_TOO_SMALL)) {
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

  std::vector<mx_handle_t> handles(hlen);

  if (blen > 0) {
    err = Dart_TypedDataAcquireData(data, &typ, &bytes, &bytes_len);
    FTL_DCHECK(!Dart_IsError(err));
  }

  rv = mx_channel_read(handle, options, bytes, hlen ? handles.data() : nullptr,
                       blen, hlen, &blen, &hlen);

  if (blen > 0) {
    err = Dart_TypedDataReleaseData(data);
    FTL_DCHECK(!Dart_IsError(err));
  }

  dart_handles = handle_table.AddAndWrap(handles.data(), hlen, dart_handles);

  Dart_ListSetAt(result, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(result, 1, data);
  Dart_ListSetAt(result, 2, dart_handles);
  Dart_ListSetAt(result, 3, Dart_NewInteger(blen));
  Dart_ListSetAt(result, 4, Dart_NewInteger(hlen));
}

void MxEventpair_Create(Dart_NativeArguments arguments) {
  int64_t options = 0;
  CHECK_INTEGER_ARGUMENT(0, &options, Null);

  mx_handle_t end1 = MX_HANDLE_INVALID;
  mx_handle_t end2 = MX_HANDLE_INVALID;
  mx_status_t rv = mx_eventpair_create(options, &end1, &end2);

  tonic::HandleTable& handle_table = tonic::HandleTable::Current();

  Dart_Handle list = Dart_NewList(3);
  Dart_ListSetAt(list, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(list, 1, handle_table.AddAndWrap(end1));
  Dart_ListSetAt(list, 2, handle_table.AddAndWrap(end2));
  Dart_SetReturnValue(arguments, list);
}

void MxSocket_Create(Dart_NativeArguments arguments) {
  int64_t options = 0;
  CHECK_INTEGER_ARGUMENT(0, &options, Null);

  mx_handle_t end1 = MX_HANDLE_INVALID;
  mx_handle_t end2 = MX_HANDLE_INVALID;
  mx_status_t rv = mx_socket_create(options, &end1, &end2);

  tonic::HandleTable& handle_table = tonic::HandleTable::Current();

  Dart_Handle list = Dart_NewList(3);
  Dart_ListSetAt(list, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(list, 1, handle_table.AddAndWrap(end1));
  Dart_ListSetAt(list, 2, handle_table.AddAndWrap(end2));
  Dart_SetReturnValue(arguments, list);
}

void MxSocket_Write(Dart_NativeArguments arguments) {
  mx_handle_t handle = MX_HANDLE_INVALID;
  tonic::HandleTable& handle_table = tonic::HandleTable::Current();
  CHECK_HANDLE_ARGUMENT(0, &handle, InvalidArgument);

  Dart_Handle typed_data = Dart_GetNativeArgument(arguments, 1);
  if (!Dart_IsTypedData(typed_data) && !Dart_IsNull(typed_data)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t offset = 0;
  CHECK_INTEGER_ARGUMENT(2, &offset, InvalidArgument);
  if (offset < 0) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t num_bytes = 0;
  CHECK_INTEGER_ARGUMENT(3, &num_bytes, InvalidArgument);
  if ((Dart_IsNull(typed_data) && (num_bytes != 0)) ||
      (!Dart_IsNull(typed_data) && (num_bytes <= 0))) {
    SetInvalidArgumentReturn(arguments);
    return;
  }
  size_t blen = static_cast<size_t>(num_bytes);

  int64_t options = 0;
  CHECK_INTEGER_ARGUMENT(4, &options, InvalidArgument);

  // Grab the data if there is any.
  Dart_TypedData_Type typ;
  void* bytes = nullptr;
  intptr_t bdlen = 0;
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataAcquireData(typed_data, &typ, &bytes, &bdlen);
  }
  bytes = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(bytes) + offset);

  mx_status_t rv = mx_socket_write(static_cast<mx_handle_t>(handle), options,
                                   const_cast<const void*>(bytes), blen, &blen);

  // Release the data.
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataReleaseData(typed_data);
  }

  Dart_Handle list = Dart_NewList(2);
  Dart_ListSetAt(list, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(list, 1, Dart_NewInteger(blen));
  Dart_SetReturnValue(arguments, list);
}

void MxSocket_Read(Dart_NativeArguments arguments) {
  mx_handle_t handle = MX_HANDLE_INVALID;
  tonic::HandleTable& handle_table = tonic::HandleTable::Current();
  CHECK_HANDLE_ARGUMENT(0, &handle, InvalidArgument);

  Dart_Handle typed_data = Dart_GetNativeArgument(arguments, 1);
  if (!Dart_IsTypedData(typed_data) && !Dart_IsNull(typed_data)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t offset = 0;
  CHECK_INTEGER_ARGUMENT(2, &offset, InvalidArgument);
  if (offset < 0) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t num_bytes = 0;
  CHECK_INTEGER_ARGUMENT(3, &num_bytes, InvalidArgument);
  if ((Dart_IsNull(typed_data) && (num_bytes != 0)) ||
      (!Dart_IsNull(typed_data) && (num_bytes <= 0))) {
    SetInvalidArgumentReturn(arguments);
    return;
  }
  size_t blen = static_cast<size_t>(num_bytes);

  int64_t options = 0;
  CHECK_INTEGER_ARGUMENT(4, &options, InvalidArgument);

  // Grab the data if there is any.
  Dart_TypedData_Type typ;
  void* bytes = nullptr;
  intptr_t bdlen = 0;
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataAcquireData(typed_data, &typ, &bytes, &bdlen);
  }
  bytes = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(bytes) + offset);

  mx_status_t rv = mx_socket_read(static_cast<mx_handle_t>(handle), options,
                                  bytes, blen, &blen);

  // Release the data.
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataReleaseData(typed_data);
  }

  Dart_Handle list = Dart_NewList(2);
  Dart_ListSetAt(list, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(list, 1, Dart_NewInteger(blen));
  Dart_SetReturnValue(arguments, list);
}

void MxVmo_Create(Dart_NativeArguments arguments) {
  int64_t size = 0;
  CHECK_INTEGER_ARGUMENT(0, &size, Null);
  if (size < 0) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t options = 0;
  CHECK_INTEGER_ARGUMENT(1, &options, Null);

  mx_handle_t vmo = MX_HANDLE_INVALID;
  mx_status_t rv = mx_vmo_create(size, options, &vmo);

  tonic::HandleTable& handle_table = tonic::HandleTable::Current();

  Dart_Handle list = Dart_NewList(2);
  Dart_ListSetAt(list, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(list, 1, handle_table.AddAndWrap(vmo));
  Dart_SetReturnValue(arguments, list);
}

void MxVmo_GetSize(Dart_NativeArguments arguments) {
  mx_handle_t handle = MX_HANDLE_INVALID;
  tonic::HandleTable& handle_table = tonic::HandleTable::Current();
  CHECK_HANDLE_ARGUMENT(0, &handle, InvalidArgument);

  uint64_t size;
  mx_status_t rv = mx_vmo_get_size(handle, &size);

  Dart_Handle list = Dart_NewList(2);
  Dart_ListSetAt(list, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(list, 1, Dart_NewInteger(size));
  Dart_SetReturnValue(arguments, list);
}

void MxVmo_SetSize(Dart_NativeArguments arguments) {
  mx_handle_t handle = MX_HANDLE_INVALID;
  tonic::HandleTable& handle_table = tonic::HandleTable::Current();
  CHECK_HANDLE_ARGUMENT(0, &handle, InvalidArgument);

  int64_t size = 0;
  CHECK_INTEGER_ARGUMENT(1, &size, Null);
  if (size < 0) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  mx_status_t rv = mx_vmo_set_size(handle, size);
  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(rv));
}

void MxVmo_Write(Dart_NativeArguments arguments) {
  mx_handle_t handle = MX_HANDLE_INVALID;
  tonic::HandleTable& handle_table = tonic::HandleTable::Current();
  CHECK_HANDLE_ARGUMENT(0, &handle, InvalidArgument);

  int64_t vmo_offset = 0;
  CHECK_INTEGER_ARGUMENT(1, &vmo_offset, InvalidArgument);
  if (vmo_offset < 0) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  Dart_Handle typed_data = Dart_GetNativeArgument(arguments, 2);
  if (!Dart_IsTypedData(typed_data) && !Dart_IsNull(typed_data)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t data_offset = 0;
  CHECK_INTEGER_ARGUMENT(3, &data_offset, InvalidArgument);
  if (data_offset < 0) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t num_bytes = 0;
  CHECK_INTEGER_ARGUMENT(4, &num_bytes, InvalidArgument);
  if ((Dart_IsNull(typed_data) && (num_bytes != 0)) ||
      (!Dart_IsNull(typed_data) && (num_bytes <= 0))) {
    SetInvalidArgumentReturn(arguments);
    return;
  }
  size_t blen = static_cast<size_t>(num_bytes);

  // Grab the data if there is any.
  Dart_TypedData_Type typ;
  void* bytes = nullptr;
  intptr_t bdlen = 0;
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataAcquireData(typed_data, &typ, &bytes, &bdlen);
  }
  bytes =
      reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(bytes) + data_offset);

  mx_status_t rv = mx_vmo_write(handle, const_cast<const void*>(bytes),
                                vmo_offset, blen, &blen);

  // Release the data.
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataReleaseData(typed_data);
  }

  Dart_Handle list = Dart_NewList(2);
  Dart_ListSetAt(list, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(list, 1, Dart_NewInteger(blen));
  Dart_SetReturnValue(arguments, list);
}

void MxVmo_Read(Dart_NativeArguments arguments) {
  mx_handle_t handle = MX_HANDLE_INVALID;
  tonic::HandleTable& handle_table = tonic::HandleTable::Current();
  CHECK_HANDLE_ARGUMENT(0, &handle, InvalidArgument);

  int64_t vmo_offset = 0;
  CHECK_INTEGER_ARGUMENT(1, &vmo_offset, InvalidArgument);
  if (vmo_offset < 0) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  Dart_Handle typed_data = Dart_GetNativeArgument(arguments, 2);
  if (!Dart_IsTypedData(typed_data) && !Dart_IsNull(typed_data)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t data_offset = 0;
  CHECK_INTEGER_ARGUMENT(3, &data_offset, InvalidArgument);
  if (data_offset < 0) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t num_bytes = 0;
  CHECK_INTEGER_ARGUMENT(4, &num_bytes, InvalidArgument);
  if ((Dart_IsNull(typed_data) && (num_bytes != 0)) ||
      (!Dart_IsNull(typed_data) && (num_bytes <= 0))) {
    SetInvalidArgumentReturn(arguments);
    return;
  }
  size_t blen = static_cast<size_t>(num_bytes);

  // Grab the data if there is any.
  Dart_TypedData_Type typ;
  void* bytes = nullptr;
  intptr_t bdlen = 0;
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataAcquireData(typed_data, &typ, &bytes, &bdlen);
  }
  bytes =
      reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(bytes) + data_offset);

  mx_status_t rv = mx_vmo_read(static_cast<mx_handle_t>(handle), bytes,
                               vmo_offset, blen, &blen);

  // Release the data.
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataReleaseData(typed_data);
  }

  Dart_Handle list = Dart_NewList(2);
  Dart_ListSetAt(list, 0, Dart_NewInteger(rv));
  Dart_ListSetAt(list, 1, Dart_NewInteger(blen));
  Dart_SetReturnValue(arguments, list);
}

}  // namespace dart
}  // namespace fidl
