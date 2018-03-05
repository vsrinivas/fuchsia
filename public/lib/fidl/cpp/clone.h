// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_CLONE_H_
#define LIB_FIDL_CPP_CLONE_H_

#include <fidl/cpp/array.h>

#include <memory>

#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fidl/cpp/traits.h"
#include "lib/fidl/cpp/vector.h"

namespace fidl {

template <typename T>
inline typename std::enable_if<IsPrimitive<T>::value, zx_status_t>::type Clone(
    const T& value,
    T* result) {
  *result = value;
  return ZX_OK;
}

template <typename T>
inline typename std::enable_if<std::is_base_of<zx::object_base, T>::value,
                               zx_status_t>::type
Clone(const T& value, T* result) {
  if (!value) {
    result->reset();
    return ZX_OK;
  }
  return value.duplicate(ZX_RIGHT_SAME_RIGHTS, result);
}

zx_status_t Clone(const zx::channel& value, zx::channel* result);

template <typename T>
inline zx_status_t Clone(const std::unique_ptr<T>& value,
                         std::unique_ptr<T>* result) {
  if (!value) {
    result->reset();
    return ZX_OK;
  }
  *result = std::make_unique<T>();
  return Clone(*value, result->get());
}

template <typename T>
inline zx_status_t Clone(const VectorPtr<T>& value, VectorPtr<T>* result) {
  if (!value) {
    *result = VectorPtr<T>();
    return ZX_OK;
  }
  result->resize(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    zx_status_t status = Clone(value[i], &result->at(i));
    if (status != ZX_OK)
      return status;
  }
  return ZX_OK;
}

template <typename T, size_t N>
inline zx_status_t Clone(const Array<T, N>& value, Array<T, N>* result) {
  for (size_t i = 0; i < N; ++i) {
    zx_status_t status = Clone(value[i], &result->at(i));
    if (status != ZX_OK)
      return status;
  }
  return ZX_OK;
}

zx_status_t Clone(const StringPtr& value, StringPtr* result);

template <typename T>
inline zx_status_t Clone(const InterfaceHandle<T>& value,
                         InterfaceHandle<T>* result) {
  if (!value) {
    *result = InterfaceHandle<T>();
    return ZX_OK;
  }
  return ZX_ERR_ACCESS_DENIED;
}

template <typename T>
inline zx_status_t Clone(const InterfaceRequest<T>& value,
                         InterfaceRequest<T>* result) {
  if (!value) {
    *result = InterfaceRequest<T>();
    return ZX_OK;
  }
  return ZX_ERR_ACCESS_DENIED;
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_CODING_TRAITS_H_
