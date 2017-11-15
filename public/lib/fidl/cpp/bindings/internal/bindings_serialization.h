// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_BINDINGS_SERIALIZATION_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_BINDINGS_SERIALIZATION_H_

#include <zircon/types.h>

#include <vector>

#include "lib/fidl/cpp/bindings/internal/bindings_internal.h"

namespace fidl {

template <typename I>
class InterfaceHandle;

namespace internal {

// Please note that this is a different value than |fidl::kInvalidHandleValue|,
// which is the "decoded" invalid handle.
const zx_handle_t kEncodedInvalidHandleValue = static_cast<zx_handle_t>(-1);

size_t Align(size_t size);
char* AlignPointer(char* ptr);

bool IsAligned(const void* ptr);

// Pointers are encoded as relative offsets. The offsets are relative to the
// address of where the offset value is stored, such that the pointer may be
// recovered with the expression:
//
//   ptr = reinterpret_cast<char*>(offset) + *offset
//
// A null pointer is encoded as an offset value of 0.
//
void EncodePointer(const void* ptr, uint64_t* offset);
// Note: This function doesn't validate the encoded pointer value.
const void* DecodePointerRaw(const uint64_t* offset);

// Note: This function doesn't validate the encoded pointer value.
template <typename T>
inline void DecodePointer(const uint64_t* offset, T** ptr) {
  *ptr = reinterpret_cast<T*>(const_cast<void*>(DecodePointerRaw(offset)));
}

// Handles are encoded as indices into a vector of handles. These functions
// manipulate the value of |handle|, mapping it to and from an index.

void EncodeHandle(WrappedHandle* handle, std::vector<zx_handle_t>* handles);
void EncodeHandle(Interface_Data* data, std::vector<zx_handle_t>* handles);
// Note: The following three functions don't validate the encoded handle value.
void DecodeHandle(WrappedHandle* handle, std::vector<zx_handle_t>* handles);
void DecodeHandle(Interface_Data* data, std::vector<zx_handle_t>* handles);

// The following 2 functions are used to encode/decode all objects (structs and
// arrays) in a consistent manner.

template <typename T>
inline void Encode(T* obj, std::vector<zx_handle_t>* handles) {
  if (obj->ptr)
    obj->ptr->EncodePointersAndHandles(handles);
  EncodePointer(obj->ptr, &obj->offset);
}

// Note: This function doesn't validate the encoded pointer and handle values.
template <typename T>
inline void Decode(T* obj, std::vector<zx_handle_t>* handles) {
  DecodePointer(&obj->offset, &obj->ptr);
  if (obj->ptr)
    obj->ptr->DecodePointersAndHandles(handles);
}

template <typename T>
inline void InterfaceHandleToData(InterfaceHandle<T> input,
                                  Interface_Data* output) {
  output->handle = WrappedHandle{input.PassHandle().release()};
}

template <typename T>
inline void InterfaceDataToHandle(Interface_Data* input,
                                  InterfaceHandle<T>* output) {
  *output = InterfaceHandle<T>(
      UnwrapHandle<zx::channel>(FetchAndReset(&input->handle)));
}

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_BINDINGS_SERIALIZATION_H_
