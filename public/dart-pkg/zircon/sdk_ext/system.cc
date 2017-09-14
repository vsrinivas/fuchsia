// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dart-pkg/zircon/sdk_ext/system.h"

#include <array>

#include "lib/tonic/dart_binding_macros.h"
#include "lib/tonic/dart_class_library.h"

using tonic::ToDart;

namespace zircon {
namespace dart {

namespace {

constexpr char kGetSizeResult[] = "GetSizeResult";
constexpr char kHandlePairResult[] = "HandlePairResult";
constexpr char kHandleResult[] = "HandleResult";
constexpr char kReadResult[] = "ReadResult";
constexpr char kWriteResult[] = "WriteResult";

class ByteDataScope {
 public:
  explicit ByteDataScope(Dart_Handle dart_handle) : dart_handle_(dart_handle) {
    Acquire();
  }

  explicit ByteDataScope(size_t size) {
    dart_handle_ = Dart_NewTypedData(Dart_TypedData_kByteData, size);
    FXL_DCHECK(!tonic::LogIfError(dart_handle_));
    Acquire();
    FXL_DCHECK(size == size_);
  }

  ~ByteDataScope() {
    if (is_valid_) {
      Release();
    }
  }

  void* data() const { return data_; }
  size_t size() const { return size_; }
  Dart_Handle dart_handle() const { return dart_handle_; }
  bool is_valid() const { return is_valid_; }

  void Release() {
    FXL_DCHECK(is_valid_);
    Dart_Handle result = Dart_TypedDataReleaseData(dart_handle_);
    tonic::LogIfError(result);
    is_valid_ = false;
    data_ = nullptr;
    size_ = 0;
  }

 private:
  void Acquire() {
    FXL_DCHECK(size_ == 0);
    FXL_DCHECK(data_ == nullptr);
    FXL_DCHECK(!is_valid_);

    Dart_TypedData_Type type;
    intptr_t size;
    Dart_Handle result =
        Dart_TypedDataAcquireData(dart_handle_, &type, &data_, &size);
    is_valid_ =
        !tonic::LogIfError(result) && type == Dart_TypedData_kByteData && data_;
    if (is_valid_) {
      size_ = size;
    } else {
      size_ = 0;
    }
  }

  Dart_Handle dart_handle_;
  bool is_valid_ = false;
  size_t size_ = 0;
  void* data_ = nullptr;
};

Dart_Handle MakeHandleList(const std::vector<zx_handle_t>& in_handles) {
  Dart_Handle list = Dart_NewList(in_handles.size());
  if (Dart_IsError(list))
    return list;
  for (size_t i = 0; i < in_handles.size(); i++) {
    Dart_Handle result =
        Dart_ListSetAt(list, i, ToDart(Handle::Create(in_handles[i])));
    if (Dart_IsError(result))
      return result;
  }
  return list;
}

template <class... Args>
Dart_Handle ConstructDartObject(const char* class_name, Args&&... args) {
  tonic::DartClassLibrary& class_library =
      tonic::DartState::Current()->class_library();
  Dart_Handle type = Dart_HandleFromPersistent(
      class_library.GetClass("zircon", class_name));
  FXL_DCHECK(!tonic::LogIfError(type));

  const char* cstr;
  Dart_StringToCString(Dart_ToString(type), &cstr);

  std::array<Dart_Handle, sizeof...(Args)> args_array{
      {std::forward<Args>(args)...}};
  Dart_Handle object =
      Dart_New(type, Dart_EmptyString(), sizeof...(Args), args_array.data());
  FXL_DCHECK(!tonic::LogIfError(object));
  return object;
}

}  // namespace

IMPLEMENT_WRAPPERTYPEINFO(zircon, System);

Dart_Handle System::ChannelCreate(uint32_t options) {
  zx_handle_t out0 = 0, out1 = 0;
  zx_status_t status = zx_channel_create(options, &out0, &out1);
  if (status != ZX_OK) {
    return ConstructDartObject(kHandlePairResult, ToDart(status));
  } else {
    return ConstructDartObject(kHandlePairResult, ToDart(status),
                               ToDart(Handle::Create(out0)),
                               ToDart(Handle::Create(out1)));
  }
}

zx_status_t System::ChannelWrite(fxl::RefPtr<Handle> channel,
                                 const tonic::DartByteData& data,
                                 std::vector<Handle*> handles) {
  if (!channel || !channel->is_valid()) {
    return ZX_ERR_BAD_HANDLE;
  }

  std::vector<zx_handle_t> zx_handles;
  for (Handle* handle : handles) {
    zx_handles.push_back(handle->handle());
  }

  zx_status_t status = zx_channel_write(channel->handle(), 0, data.data(),
                                        data.length_in_bytes(),
                                        zx_handles.data(), zx_handles.size());
  if (status == ZX_OK) {
    // Handles were transferred.
    for (Handle* handle : handles) {
      handle->ReleaseHandle();
    }
  }

  return status;
}

Dart_Handle System::ChannelQueryAndRead(fxl::RefPtr<Handle> channel) {
  if (!channel || !channel->is_valid()) {
    return ConstructDartObject(kReadResult, ToDart(ZX_ERR_BAD_HANDLE));
  }

  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;

  // Query the size of the next message.
  zx_status_t status = zx_channel_read(channel->handle(), 0, nullptr, nullptr,
                                       0, 0, &actual_bytes, &actual_handles);
  if (status != ZX_ERR_BUFFER_TOO_SMALL) {
    // An empty message or an error.
    return ConstructDartObject(kReadResult, ToDart(status));
  }

  // Allocate space for the bytes and handles.
  ByteDataScope bytes(actual_bytes);
  FXL_DCHECK(bytes.is_valid());
  std::vector<zx_handle_t> handles(actual_handles);

  // Make the call to actually get the message.
  status = zx_channel_read(channel->handle(), 0, bytes.data(), handles.data(),
                           bytes.size(), handles.size(), &actual_bytes,
                           &actual_handles);
  FXL_DCHECK(status != ZX_OK || bytes.size() == actual_bytes);

  bytes.Release();

  if (status == ZX_OK) {
    FXL_DCHECK(handles.size() == actual_handles);

    // return a ReadResult object.
    return ConstructDartObject(kReadResult, ToDart(status), bytes.dart_handle(),
                               ToDart(actual_bytes), MakeHandleList(handles));
  } else {
    return ConstructDartObject(kReadResult, ToDart(status));
  }
}

Dart_Handle System::EventpairCreate(uint32_t options) {
  zx_handle_t out0 = 0, out1 = 0;
  zx_status_t status = zx_eventpair_create(0, &out0, &out1);
  if (status != ZX_OK) {
    return ConstructDartObject(kHandlePairResult, ToDart(status));
  } else {
    return ConstructDartObject(kHandlePairResult, ToDart(status),
                               ToDart(Handle::Create(out0)),
                               ToDart(Handle::Create(out1)));
  }
}

Dart_Handle System::SocketCreate(uint32_t options) {
  zx_handle_t out0 = 0, out1 = 0;
  zx_status_t status = zx_socket_create(options, &out0, &out1);
  if (status != ZX_OK) {
    return ConstructDartObject(kHandlePairResult, ToDart(status));
  } else {
    return ConstructDartObject(kHandlePairResult, ToDart(status),
                               ToDart(Handle::Create(out0)),
                               ToDart(Handle::Create(out1)));
  }
}

Dart_Handle System::SocketWrite(fxl::RefPtr<Handle> socket,
                                const tonic::DartByteData& data,
                                int options) {
  if (!socket || !socket->is_valid()) {
    data.Release();
    return ConstructDartObject(kWriteResult, ToDart(ZX_ERR_BAD_HANDLE));
  }

  size_t actual;
  zx_status_t status = zx_socket_write(socket->handle(), options, data.data(),
                                       data.length_in_bytes(), &actual);
  data.Release();
  return ConstructDartObject(kWriteResult, ToDart(status), ToDart(actual));
}

Dart_Handle System::SocketRead(fxl::RefPtr<Handle> socket, size_t size) {
  if (!socket || !socket->is_valid()) {
    return ConstructDartObject(kReadResult, ToDart(ZX_ERR_BAD_HANDLE));
  }

  ByteDataScope bytes(size);
  size_t actual;
  zx_status_t status =
      zx_socket_read(socket->handle(), 0, bytes.data(), size, &actual);
  bytes.Release();
  if (status == ZX_OK) {
    FXL_DCHECK(actual <= size);
    return ConstructDartObject(kReadResult, ToDart(status), bytes.dart_handle(),
                               ToDart(actual));
  }

  return ConstructDartObject(kReadResult, ToDart(status));
}

Dart_Handle System::VmoCreate(uint64_t size, uint32_t options) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  zx_status_t status = zx_vmo_create(size, options, &vmo);
  if (status != ZX_OK) {
    return ConstructDartObject(kHandleResult, ToDart(status));
  } else {
    return ConstructDartObject(kHandleResult, ToDart(status),
                               ToDart(Handle::Create(vmo)));
  }
}

Dart_Handle System::VmoGetSize(fxl::RefPtr<Handle> vmo) {
  if (!vmo || !vmo->is_valid()) {
    return ConstructDartObject(kGetSizeResult, ToDart(ZX_ERR_BAD_HANDLE));
  }

  uint64_t size;
  zx_status_t status = zx_vmo_get_size(vmo->handle(), &size);

  return ConstructDartObject(kGetSizeResult, ToDart(status), ToDart(size));
}

zx_status_t System::VmoSetSize(fxl::RefPtr<Handle> vmo, uint64_t size) {
  if (!vmo || !vmo->is_valid()) {
    return ZX_ERR_BAD_HANDLE;
  }
  return zx_vmo_set_size(vmo->handle(), size);
}

Dart_Handle System::VmoWrite(fxl::RefPtr<Handle> vmo,
                             uint64_t offset,
                             const tonic::DartByteData& data) {
  if (!vmo || !vmo->is_valid()) {
    return ConstructDartObject(kWriteResult, ToDart(ZX_ERR_BAD_HANDLE));
  }

  size_t actual;
  zx_status_t status = zx_vmo_write(vmo->handle(), data.data(), offset,
                                    data.length_in_bytes(), &actual);

  return ConstructDartObject(kWriteResult, ToDart(status), ToDart(actual));
}

Dart_Handle System::VmoRead(fxl::RefPtr<Handle> vmo,
                            uint64_t offset,
                            size_t size) {
  if (!vmo || !vmo->is_valid()) {
    return ConstructDartObject(kReadResult, ToDart(ZX_ERR_BAD_HANDLE));
  }

  // TODO: constrain size?
  ByteDataScope bytes(size);
  size_t actual;
  zx_status_t status =
      zx_vmo_read(vmo->handle(), bytes.data(), offset, size, &actual);
  bytes.Release();
  if (status == ZX_OK) {
    FXL_DCHECK(actual <= size);
    return ConstructDartObject(kReadResult, ToDart(status), bytes.dart_handle(),
                               ToDart(size));
  } else {
    return ConstructDartObject(kReadResult, ToDart(status));
  }
}

uint64_t System::TimeGet(uint32_t clock_id) {
  return zx_time_get(clock_id);
}

  // clang-format: off

#define FOR_EACH_STATIC_BINDING(V) \
  V(System, ChannelCreate)         \
  V(System, ChannelWrite)          \
  V(System, ChannelQueryAndRead)   \
  V(System, EventpairCreate)       \
  V(System, SocketCreate)          \
  V(System, SocketWrite)           \
  V(System, SocketRead)            \
  V(System, VmoCreate)             \
  V(System, VmoGetSize)            \
  V(System, VmoSetSize)            \
  V(System, VmoRead)               \
  V(System, VmoWrite)              \
  V(System, TimeGet)

// clang-format: on

// Tonic is missing a comma.
#define DART_REGISTER_NATIVE_STATIC_(CLASS, METHOD) \
  DART_REGISTER_NATIVE_STATIC(CLASS, METHOD),

FOR_EACH_STATIC_BINDING(DART_NATIVE_CALLBACK_STATIC)

void System::RegisterNatives(tonic::DartLibraryNatives* natives) {
  natives->Register({FOR_EACH_STATIC_BINDING(DART_REGISTER_NATIVE_STATIC_)});
}

}  // namespace dart
}  // namespace zircon
