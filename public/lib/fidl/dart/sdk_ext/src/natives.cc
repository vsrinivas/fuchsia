// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/buffer.h>
#include <mojo/system/data_pipe.h>
#include <mojo/system/handle.h>
#include <mojo/system/message_pipe.h>
#include <mojo/system/result.h>
#include <mojo/system/time.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <vector>

#include "dart/runtime/include/dart_api.h"
#include "mojo/public/cpp/environment/logging.h"
#include "mojo/public/cpp/system/macros.h"
#include "mojo/public/cpp/system/wait.h"
#include "mojo/public/platform/dart/dart_handle_watcher.h"

namespace mojo {
namespace dart {

#define REGISTER_FUNCTION(name, count)                                         \
  { "" #name, name, count },
#define DECLARE_FUNCTION(name, count)                                          \
  extern void name(Dart_NativeArguments args);

#define MOJO_NATIVE_LIST(V)                \
  V(MojoSharedBuffer_Create, 2)            \
  V(MojoSharedBuffer_Duplicate, 2)         \
  V(MojoSharedBuffer_Map, 4)               \
  V(MojoSharedBuffer_GetInformation, 1)    \
  V(MojoDataPipe_Create, 3)                \
  V(MojoDataPipe_WriteData, 4)             \
  V(MojoDataPipe_BeginWriteData, 2)        \
  V(MojoDataPipe_EndWriteData, 2)          \
  V(MojoDataPipe_ReadData, 4)              \
  V(MojoDataPipe_BeginReadData, 2)         \
  V(MojoDataPipe_EndReadData, 2)           \
  V(MojoMessagePipe_Create, 1)             \
  V(MojoMessagePipe_Write, 5)              \
  V(MojoMessagePipe_Read, 5)               \
  V(MojoMessagePipe_QueryAndRead, 3)       \
  V(Mojo_GetTimeTicksNow, 0)               \
  V(MojoHandle_Close, 1)                   \
  V(MojoHandle_Wait, 3)                    \
  V(MojoHandle_RegisterFinalizer, 2)       \
  V(MojoHandle_WaitMany, 3)                \
  V(MojoHandleWatcher_SendControlData, 5)

MOJO_NATIVE_LIST(DECLARE_FUNCTION);

static struct NativeEntries {
  const char* name;
  Dart_NativeFunction function;
  int argument_count;
} MojoEntries[] = {MOJO_NATIVE_LIST(REGISTER_FUNCTION)};

Dart_NativeFunction MojoNativeLookup(Dart_Handle name,
                                     int argument_count,
                                     bool* auto_setup_scope) {
  const char* function_name = nullptr;
  Dart_Handle result = Dart_StringToCString(name, &function_name);
  DART_CHECK_VALID(result);
  assert(function_name != nullptr);
  assert(auto_setup_scope != nullptr);
  *auto_setup_scope = true;
  size_t num_entries = MOJO_ARRAYSIZE(MojoEntries);
  for (size_t i = 0; i < num_entries; ++i) {
    const struct NativeEntries& entry = MojoEntries[i];
    if (!strcmp(function_name, entry.name) &&
        (entry.argument_count == argument_count)) {
      return entry.function;
    }
  }
  return nullptr;
}

const uint8_t* MojoNativeSymbol(Dart_NativeFunction nf) {
  size_t num_entries = MOJO_ARRAYSIZE(MojoEntries);
  for (size_t i = 0; i < num_entries; ++i) {
    const struct NativeEntries& entry = MojoEntries[i];
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
  Dart_SetIntegerReturnValue(
      arguments, static_cast<int64_t>(MOJO_SYSTEM_RESULT_INVALID_ARGUMENT));
}

static Dart_Handle SignalsStateToDart(const MojoHandleSignalsState& state) {
  Dart_Handle list = Dart_NewList(2);
  Dart_Handle arg0 = Dart_NewInteger(state.satisfied_signals);
  Dart_Handle arg1 = Dart_NewInteger(state.satisfiable_signals);
  Dart_ListSetAt(list, 0, arg0);
  Dart_ListSetAt(list, 1, arg1);
  return list;
}

#define CHECK_INTEGER_ARGUMENT(args, num, result, failure)                     \
  {                                                                            \
    Dart_Handle __status;                                                      \
    __status = Dart_GetNativeIntegerArgument(args, num, result);               \
    if (Dart_IsError(__status)) {                                              \
      Set##failure##Return(arguments);                                         \
      return;                                                                  \
    }                                                                          \
  }                                                                            \

struct CloserCallbackPeer {
  MojoHandle handle;
};

static void MojoHandleCloserCallback(void* isolate_data,
                                     Dart_WeakPersistentHandle handle,
                                     void* peer) {
  CloserCallbackPeer* callback_peer =
      reinterpret_cast<CloserCallbackPeer*>(peer);
  if (callback_peer->handle != MOJO_HANDLE_INVALID) {
    MojoClose(callback_peer->handle);
  }
  delete callback_peer;
}

// Setup a weak persistent handle for a MojoHandle that calls MojoClose on the
// handle when the MojoHandle is GC'd or the VM is going down.
void MojoHandle_RegisterFinalizer(Dart_NativeArguments arguments) {
  Dart_Handle mojo_handle_instance = Dart_GetNativeArgument(arguments, 0);
  if (!Dart_IsInstance(mojo_handle_instance)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t raw_handle = static_cast<int64_t>(MOJO_HANDLE_INVALID);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &raw_handle, InvalidArgument);
  if (raw_handle == static_cast<int64_t>(MOJO_HANDLE_INVALID)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  // Add the handle to this isolate's set.
  MojoHandle handle = static_cast<MojoHandle>(raw_handle);

  // Set up a finalizer.
  CloserCallbackPeer* callback_peer = new CloserCallbackPeer();
  callback_peer->handle = handle;
  Dart_NewWeakPersistentHandle(mojo_handle_instance,
                               reinterpret_cast<void*>(callback_peer),
                               sizeof(CloserCallbackPeer),
                               MojoHandleCloserCallback);
  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(MOJO_RESULT_OK));
}

void Mojo_GetTimeTicksNow(Dart_NativeArguments arguments) {
  MojoTimeTicks ticks = MojoGetTimeTicksNow();
  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(ticks));
}

void MojoHandle_Close(Dart_NativeArguments arguments) {
  int64_t raw_handle;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &raw_handle, InvalidArgument);

  // Remove the handle from this isolate's set.
  MojoHandle handle = static_cast<MojoHandle>(raw_handle);
  MojoResult res = MojoClose(handle);

  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(res));
}

void MojoHandle_Wait(Dart_NativeArguments arguments) {
  int64_t handle = 0;
  int64_t signals = 0;
  int64_t deadline = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &handle, InvalidArgument);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &signals, InvalidArgument);
  CHECK_INTEGER_ARGUMENT(arguments, 2, &deadline, InvalidArgument);

  MojoHandleSignalsState state;
  MojoResult r = mojo::Wait(mojo::Handle(static_cast<MojoHandle>(handle)),
                            static_cast<MojoHandleSignals>(signals),
                            static_cast<MojoDeadline>(deadline), &state);

  // The return value is structured as a list of length 2:
  // [0] MojoResult
  // [1] MojoHandleSignalsState. (may be null)
  Dart_Handle list = Dart_NewList(2);
  Dart_ListSetAt(list, 0, Dart_NewInteger(r));
  if (mojo::WaitManyResult(r).AreSignalsStatesValid()) {
    Dart_ListSetAt(list, 1, SignalsStateToDart(state));
  } else {
    Dart_ListSetAt(list, 1, Dart_Null());
  }
  Dart_SetReturnValue(arguments, list);
}

void MojoHandle_WaitMany(Dart_NativeArguments arguments) {
  int64_t deadline = 0;
  Dart_Handle handles = Dart_GetNativeArgument(arguments, 0);
  Dart_Handle signals = Dart_GetNativeArgument(arguments, 1);
  CHECK_INTEGER_ARGUMENT(arguments, 2, &deadline, InvalidArgument);

  if (!Dart_IsList(handles) || !Dart_IsList(signals)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  intptr_t handles_len = 0;
  intptr_t signals_len = 0;
  Dart_ListLength(handles, &handles_len);
  Dart_ListLength(signals, &signals_len);
  if (handles_len != signals_len) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  std::vector<mojo::Handle> mojo_handles(handles_len);
  std::vector<MojoHandleSignals> mojo_signals(handles_len);

  for (int i = 0; i < handles_len; i++) {
    Dart_Handle dart_handle = Dart_ListGetAt(handles, i);
    Dart_Handle dart_signal = Dart_ListGetAt(signals, i);
    if (!Dart_IsInteger(dart_handle) || !Dart_IsInteger(dart_signal)) {
      SetInvalidArgumentReturn(arguments);
      return;
    }
    int64_t mojo_handle = 0;
    int64_t mojo_signal = 0;
    Dart_IntegerToInt64(dart_handle, &mojo_handle);
    Dart_IntegerToInt64(dart_signal, &mojo_signal);
    mojo_handles[i] = mojo::Handle(mojo_handle);
    mojo_signals[i] = static_cast<MojoHandleSignals>(mojo_signal);
  }

  std::vector<MojoHandleSignalsState> states(handles_len);
  mojo::WaitManyResult wmr = mojo::WaitMany(
      mojo_handles, mojo_signals, static_cast<MojoDeadline>(deadline), &states);

  // The return value is structured as a list of length 3:
  // [0] MojoResult
  // [1] index of handle that caused a return (may be null)
  // [2] list of MojoHandleSignalsState. (may be null)
  Dart_Handle list = Dart_NewList(3);
  Dart_ListSetAt(list, 0, Dart_NewInteger(wmr.result));
  if (wmr.IsIndexValid())
    Dart_ListSetAt(list, 1, Dart_NewInteger(wmr.index));
  else
    Dart_ListSetAt(list, 1, Dart_Null());
  if (wmr.AreSignalsStatesValid()) {
    Dart_Handle stateList = Dart_NewList(handles_len);
    for (int i = 0; i < handles_len; i++) {
      Dart_ListSetAt(stateList, i, SignalsStateToDart(states[i]));
    }
    Dart_ListSetAt(list, 2, stateList);
  } else {
    Dart_ListSetAt(list, 2, Dart_Null());
  }
  Dart_SetReturnValue(arguments, list);
}

void MojoSharedBuffer_Create(Dart_NativeArguments arguments) {
  int64_t num_bytes = 0;
  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &num_bytes, Null);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &flags, Null);

  MojoCreateSharedBufferOptions options;
  options.struct_size = sizeof(MojoCreateSharedBufferOptions);
  options.flags = static_cast<MojoCreateSharedBufferOptionsFlags>(flags);

  MojoHandle out = MOJO_HANDLE_INVALID;;
  MojoResult res = MojoCreateSharedBuffer(
      &options, static_cast<int32_t>(num_bytes), &out);

  Dart_Handle list = Dart_NewList(2);
  Dart_ListSetAt(list, 0, Dart_NewInteger(res));
  Dart_ListSetAt(list, 1, Dart_NewInteger(out));
  Dart_SetReturnValue(arguments, list);
}

void MojoSharedBuffer_Duplicate(Dart_NativeArguments arguments) {
  int64_t handle = 0;
  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &handle, Null);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &flags, Null);

  MojoDuplicateBufferHandleOptions options;
  options.struct_size = sizeof(MojoDuplicateBufferHandleOptions);
  options.flags = static_cast<MojoDuplicateBufferHandleOptionsFlags>(flags);

  MojoHandle out = MOJO_HANDLE_INVALID;;
  MojoResult res = MojoDuplicateBufferHandle(
      static_cast<MojoHandle>(handle), &options, &out);

  Dart_Handle list = Dart_NewList(2);
  Dart_ListSetAt(list, 0, Dart_NewInteger(res));
  Dart_ListSetAt(list, 1, Dart_NewInteger(out));
  Dart_SetReturnValue(arguments, list);
}

static void MojoBufferUnmapCallback(void* isolate_data,
                                    Dart_WeakPersistentHandle handle,
                                    void* peer) {
  MojoUnmapBuffer(peer);
}

void MojoSharedBuffer_Map(Dart_NativeArguments arguments) {
  int64_t handle = 0;
  int64_t offset = 0;
  int64_t num_bytes = 0;
  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &handle, Null);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &offset, Null);
  CHECK_INTEGER_ARGUMENT(arguments, 2, &num_bytes, Null);
  CHECK_INTEGER_ARGUMENT(arguments, 3, &flags, Null);

  void* out;
  MojoResult res = MojoMapBuffer(static_cast<MojoHandle>(handle),
                                 offset,
                                 num_bytes,
                                 &out,
                                 static_cast<MojoMapBufferFlags>(flags));

  Dart_Handle list = Dart_NewList(2);
  Dart_Handle typed_data;
  if (res == MOJO_RESULT_OK) {
    typed_data = Dart_NewExternalTypedData(
        Dart_TypedData_kByteData, out, num_bytes);
    Dart_NewWeakPersistentHandle(typed_data, out, num_bytes,
                                 MojoBufferUnmapCallback);
  } else {
    typed_data = Dart_Null();
  }
  Dart_ListSetAt(list, 0, Dart_NewInteger(res));
  Dart_ListSetAt(list, 1, typed_data);
  Dart_SetReturnValue(arguments, list);
}

void MojoSharedBuffer_GetInformation(Dart_NativeArguments arguments) {
  int64_t handle = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &handle, Null);

  MojoBufferInformation buffer_information;

  MojoResult res = MojoGetBufferInformation(static_cast<MojoHandle>(handle),
                                            &buffer_information,
                                            sizeof(buffer_information));

  Dart_Handle list = Dart_NewList(3);
  Dart_ListSetAt(list, 0, Dart_NewInteger(res));
  if (res == MOJO_RESULT_OK) {
    Dart_ListSetAt(list, 1, Dart_NewInteger(buffer_information.flags));
    Dart_ListSetAt(list, 2, Dart_NewInteger(buffer_information.num_bytes));
  }

  Dart_SetReturnValue(arguments, list);
}

void MojoDataPipe_Create(Dart_NativeArguments arguments) {
  int64_t element_bytes = 0;
  int64_t capacity_bytes = 0;
  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &element_bytes, Null);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &capacity_bytes, Null);
  CHECK_INTEGER_ARGUMENT(arguments, 2, &flags, Null);

  MojoCreateDataPipeOptions options;
  options.struct_size = sizeof(MojoCreateDataPipeOptions);
  options.flags = static_cast<MojoCreateDataPipeOptionsFlags>(flags);
  options.element_num_bytes = static_cast<uint32_t>(element_bytes);
  options.capacity_num_bytes = static_cast<uint32_t>(capacity_bytes);

  MojoHandle producer = MOJO_HANDLE_INVALID;
  MojoHandle consumer = MOJO_HANDLE_INVALID;
  MojoResult res = MojoCreateDataPipe(&options, &producer, &consumer);

  Dart_Handle list = Dart_NewList(3);
  Dart_ListSetAt(list, 0, Dart_NewInteger(res));
  Dart_ListSetAt(list, 1, Dart_NewInteger(producer));
  Dart_ListSetAt(list, 2, Dart_NewInteger(consumer));
  Dart_SetReturnValue(arguments, list);
}

void MojoDataPipe_WriteData(Dart_NativeArguments arguments) {
  int64_t handle = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &handle, Null);

  Dart_Handle typed_data = Dart_GetNativeArgument(arguments, 1);
  if (!Dart_IsTypedData(typed_data)) {
    SetNullReturn(arguments);
    return;
  }

  int64_t num_bytes = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 2, &num_bytes, Null);
  if (num_bytes <= 0) {
    SetNullReturn(arguments);
    return;
  }

  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 3, &flags, Null);

  Dart_TypedData_Type type;
  void* data;
  intptr_t data_length;
  Dart_TypedDataAcquireData(typed_data, &type, &data, &data_length);
  uint32_t length = static_cast<uint32_t>(num_bytes);
  MojoResult res = MojoWriteData(
      static_cast<MojoHandle>(handle),
      data,
      &length,
      static_cast<MojoWriteDataFlags>(flags));
  Dart_TypedDataReleaseData(typed_data);

  Dart_Handle list = Dart_NewList(2);
  Dart_ListSetAt(list, 0, Dart_NewInteger(res));
  Dart_ListSetAt(list, 1, Dart_NewInteger(length));
  Dart_SetReturnValue(arguments, list);
}

void MojoDataPipe_BeginWriteData(Dart_NativeArguments arguments) {
  int64_t handle = 0;
  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &handle, Null);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &flags, Null);

  void* buffer;
  uint32_t size;
  MojoResult res = MojoBeginWriteData(
      static_cast<MojoHandle>(handle),
      &buffer,
      &size,
      static_cast<MojoWriteDataFlags>(flags));

  Dart_Handle list = Dart_NewList(2);
  Dart_Handle typed_data;
  if (res == MOJO_RESULT_OK) {
    typed_data = Dart_NewExternalTypedData(
        Dart_TypedData_kByteData, buffer, size);
  } else {
    typed_data = Dart_Null();
  }
  Dart_ListSetAt(list, 0, Dart_NewInteger(res));
  Dart_ListSetAt(list, 1, typed_data);
  Dart_SetReturnValue(arguments, list);
}

void MojoDataPipe_EndWriteData(Dart_NativeArguments arguments) {
  int64_t handle = 0;
  int64_t num_bytes_written = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &handle, InvalidArgument);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &num_bytes_written, InvalidArgument);

  MojoResult res = MojoEndWriteData(
      static_cast<MojoHandle>(handle),
      static_cast<uint32_t>(num_bytes_written));

  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(res));
}

void MojoDataPipe_ReadData(Dart_NativeArguments arguments) {
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

  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 3, &flags, Null);

  Dart_TypedData_Type typ;
  void* data = nullptr;
  intptr_t bdlen = 0;
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataAcquireData(typed_data, &typ, &data, &bdlen);
  }
  uint32_t len = static_cast<uint32_t>(num_bytes);
  MojoResult res = MojoReadData(
      static_cast<MojoHandle>(handle),
      data,
      &len,
      static_cast<MojoReadDataFlags>(flags));
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataReleaseData(typed_data);
  }

  Dart_Handle list = Dart_NewList(2);
  Dart_ListSetAt(list, 0, Dart_NewInteger(res));
  Dart_ListSetAt(list, 1, Dart_NewInteger(len));
  Dart_SetReturnValue(arguments, list);
}

void MojoDataPipe_BeginReadData(Dart_NativeArguments arguments) {
  int64_t handle = 0;
  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &handle, Null);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &flags, Null);

  void* buffer;
  uint32_t size;
  MojoResult res = MojoBeginReadData(
      static_cast<MojoHandle>(handle),
      const_cast<const void**>(&buffer),
      &size,
      static_cast<MojoWriteDataFlags>(flags));

  Dart_Handle list = Dart_NewList(2);
  Dart_Handle typed_data;
  if (res == MOJO_RESULT_OK) {
    typed_data = Dart_NewExternalTypedData(
        Dart_TypedData_kByteData, buffer, size);
  } else {
    typed_data = Dart_Null();
  }
  Dart_ListSetAt(list, 0, Dart_NewInteger(res));
  Dart_ListSetAt(list, 1, typed_data);
  Dart_SetReturnValue(arguments, list);
}

void MojoDataPipe_EndReadData(Dart_NativeArguments arguments) {
  int64_t handle = 0;
  int64_t num_bytes_read = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &handle, InvalidArgument);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &num_bytes_read, InvalidArgument);

  MojoResult res = MojoEndReadData(
      static_cast<MojoHandle>(handle),
      static_cast<uint32_t>(num_bytes_read));

  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(res));
}

void MojoMessagePipe_Create(Dart_NativeArguments arguments) {
  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &flags, Null);

  MojoCreateMessagePipeOptions options;
  options.struct_size = sizeof(MojoCreateMessagePipeOptions);
  options.flags = static_cast<MojoCreateMessagePipeOptionsFlags>(flags);

  MojoHandle end1 = MOJO_HANDLE_INVALID;
  MojoHandle end2 = MOJO_HANDLE_INVALID;
  MojoResult res = MojoCreateMessagePipe(&options, &end1, &end2);

  Dart_Handle list = Dart_NewList(3);
  Dart_ListSetAt(list, 0, Dart_NewInteger(res));
  Dart_ListSetAt(list, 1, Dart_NewInteger(end1));
  Dart_ListSetAt(list, 2, Dart_NewInteger(end2));
  Dart_SetReturnValue(arguments, list);
}

void MojoMessagePipe_Write(Dart_NativeArguments arguments) {
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

  Dart_Handle handles = Dart_GetNativeArgument(arguments, 3);
  if (!Dart_IsList(handles) && !Dart_IsNull(handles)) {
    SetInvalidArgumentReturn(arguments);
    return;
  }

  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 4, &flags, InvalidArgument);

  // Grab the data if there is any.
  Dart_TypedData_Type typ;
  void* bytes = nullptr;
  intptr_t bdlen = 0;
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataAcquireData(typed_data, &typ, &bytes, &bdlen);
  }

  // Grab the handles if there are any.
  std::unique_ptr<MojoHandle[]> mojo_handles;
  intptr_t handles_len = 0;
  if (!Dart_IsNull(handles)) {
    Dart_ListLength(handles, &handles_len);
    if (handles_len > 0) {
      mojo_handles.reset(new MojoHandle[handles_len]);
    }
    for (int i = 0; i < handles_len; i++) {
      Dart_Handle dart_handle = Dart_ListGetAt(handles, i);
      if (!Dart_IsInteger(dart_handle)) {
        SetInvalidArgumentReturn(arguments);
        return;
      }
      int64_t mojo_handle = 0;
      Dart_IntegerToInt64(dart_handle, &mojo_handle);
      mojo_handles[i] = static_cast<MojoHandle>(mojo_handle);
    }
  }

  MojoResult res = MojoWriteMessage(
      static_cast<MojoHandle>(handle),
      const_cast<const void*>(bytes),
      static_cast<uint32_t>(num_bytes),
      mojo_handles.get(),
      static_cast<uint32_t>(handles_len),
      static_cast<MojoWriteMessageFlags>(flags));

  // Release the data.
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataReleaseData(typed_data);
  }

  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(res));
}

void MojoMessagePipe_Read(Dart_NativeArguments arguments) {
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

  Dart_Handle handles = Dart_GetNativeArgument(arguments, 3);
  if (!Dart_IsList(handles) && !Dart_IsNull(handles)) {
    SetNullReturn(arguments);
    return;
  }

  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 4, &flags, Null);

  // Grab the data if there is any.
  Dart_TypedData_Type typ;
  void* bytes = nullptr;
  intptr_t byte_data_len = 0;
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataAcquireData(typed_data, &typ, &bytes, &byte_data_len);
  }
  uint32_t blen = static_cast<uint32_t>(num_bytes);

  // Grab the handles if there are any.
  std::unique_ptr<MojoHandle[]> mojo_handles;
  intptr_t handles_len = 0;
  if (!Dart_IsNull(handles)) {
    Dart_ListLength(handles, &handles_len);
    mojo_handles.reset(new MojoHandle[handles_len]);
  }
  uint32_t hlen = static_cast<uint32_t>(handles_len);

  MojoResult res = MojoReadMessage(
      static_cast<MojoHandle>(handle),
      bytes,
      &blen,
      mojo_handles.get(),
      &hlen,
      static_cast<MojoReadMessageFlags>(flags));

  // Release the data.
  if (!Dart_IsNull(typed_data)) {
    Dart_TypedDataReleaseData(typed_data);
  }

  if (!Dart_IsNull(handles)) {
    for (int i = 0; i < handles_len; i++) {
      Dart_ListSetAt(handles, i, Dart_NewInteger(mojo_handles[i]));
    }
  }

  Dart_Handle list = Dart_NewList(3);
  Dart_ListSetAt(list, 0, Dart_NewInteger(res));
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

void MojoMessagePipe_QueryAndRead(Dart_NativeArguments arguments) {
  Dart_Handle err;
  int64_t dart_handle;
  int64_t flags = 0;
  CHECK_INTEGER_ARGUMENT(arguments, 0, &dart_handle, Null);
  CHECK_INTEGER_ARGUMENT(arguments, 1, &flags, Null);
  Dart_Handle result = Dart_GetNativeArgument(arguments, 2);

  Dart_Handle data = Dart_ListGetAt(result, 1);
  Dart_Handle handles = Dart_ListGetAt(result, 2);

  // Query the number of bytes and handles available.
  uint32_t blen = 0;
  uint32_t hlen = 0;
  MojoResult res =
      MojoReadMessage(static_cast<MojoHandle>(dart_handle), nullptr, &blen,
                      nullptr, &hlen, MOJO_READ_MESSAGE_FLAG_NONE);

  if ((res != MOJO_RESULT_OK) &&
      (res != MOJO_SYSTEM_RESULT_RESOURCE_EXHAUSTED)) {
    Dart_ListSetAt(result, 0, Dart_NewInteger(res));
    Dart_ListSetAt(result, 1, data);
    Dart_ListSetAt(result, 2, handles);
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
    MOJO_DCHECK(!Dart_IsError(data));
    Dart_NewWeakPersistentHandle(data, new_byte_data, blen, ByteArrayFinalizer);
  } else if (blen > 0) {
    err = Dart_TypedDataAcquireData(data, &typ, &bytes, &bytes_len);
    MOJO_DCHECK(!Dart_IsError(err));
    err = Dart_TypedDataReleaseData(data);
    MOJO_DCHECK(!Dart_IsError(err));
    if (static_cast<uintptr_t>(bytes_len) < blen) {
      uint8_t* new_byte_data = new uint8_t[blen];
      data = Dart_NewExternalTypedData(Dart_TypedData_kByteData, new_byte_data,
                                       blen);
      MOJO_DCHECK(!Dart_IsError(data));
      Dart_NewWeakPersistentHandle(data, new_byte_data, blen,
                                   ByteArrayFinalizer);
    }
  }

  void* handle_bytes = nullptr;
  intptr_t handles_len = 0;
  if ((hlen > 0) && Dart_IsNull(handles)) {
    uint32_t* new_handle_data = new uint32_t[hlen];
    handles = Dart_NewExternalTypedData(Dart_TypedData_kUint32, new_handle_data,
                                        hlen);
    MOJO_DCHECK(!Dart_IsError(handles));
    Dart_NewWeakPersistentHandle(handles, new_handle_data,
                                 hlen * sizeof(uint32_t), HandleArrayFinalizer);
  } else if (hlen > 0) {
    err = Dart_TypedDataAcquireData(handles, &typ, &handle_bytes, &handles_len);
    MOJO_DCHECK(!Dart_IsError(err));
    err = Dart_TypedDataReleaseData(handles);
    MOJO_DCHECK(!Dart_IsError(err));
    if (static_cast<uintptr_t>(handles_len) < hlen) {
      uint32_t* new_handle_data = new uint32_t[hlen];
      handles = Dart_NewExternalTypedData(Dart_TypedData_kUint32,
                                          new_handle_data, hlen);
      MOJO_DCHECK(!Dart_IsError(handles));
      Dart_NewWeakPersistentHandle(handles, new_handle_data,
                                   hlen * sizeof(uint32_t),
                                   HandleArrayFinalizer);
    }
  }

  if (blen > 0) {
    err = Dart_TypedDataAcquireData(data, &typ, &bytes, &bytes_len);
    MOJO_DCHECK(!Dart_IsError(err));
  }

  if (hlen > 0) {
    err = Dart_TypedDataAcquireData(handles, &typ, &handle_bytes, &handles_len);
    MOJO_DCHECK(!Dart_IsError(err));
  }

  res = MojoReadMessage(static_cast<MojoHandle>(dart_handle), bytes, &blen,
                        reinterpret_cast<MojoHandle*>(handle_bytes), &hlen,
                        static_cast<MojoReadMessageFlags>(flags));

  if (blen > 0) {
    err = Dart_TypedDataReleaseData(data);
    MOJO_DCHECK(!Dart_IsError(err));
  }

  if (hlen > 0) {
    err = Dart_TypedDataReleaseData(handles);
    MOJO_DCHECK(!Dart_IsError(err));
  }

  Dart_ListSetAt(result, 0, Dart_NewInteger(res));
  Dart_ListSetAt(result, 1, data);
  Dart_ListSetAt(result, 2, handles);
  Dart_ListSetAt(result, 3, Dart_NewInteger(blen));
  Dart_ListSetAt(result, 4, Dart_NewInteger(hlen));
}


void MojoHandleWatcher_SendControlData(Dart_NativeArguments arguments) {
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

  HandleWatcherCommand command =
      HandleWatcherCommand::FromDart(command_code,
                                     handle_or_deadline,
                                     send_port_id,
                                     signals);
  MojoResult res = MojoWriteMessage(
      control_handle,
      reinterpret_cast<const void*>(&command),
      sizeof(command),
      nullptr,
      0,
      0);
  Dart_SetIntegerReturnValue(arguments, static_cast<int64_t>(res));
}

}  // namespace dart
}  // namespace mojo
