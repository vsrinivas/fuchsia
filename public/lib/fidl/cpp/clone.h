// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_CLONE_H_
#define LIB_FIDL_CPP_CLONE_H_

#include <lib/fidl/cpp/array.h>
#include <zircon/assert.h>
#include <memory>
#include "lib/fidl/cpp/string.h"
#include "lib/fidl/cpp/traits.h"
#include "lib/fidl/cpp/vector.h"

namespace fidl {

#ifdef __Fuchsia__
namespace internal {

template <typename T>
inline typename std::enable_if<zx::object_traits<T>::supports_duplication,
                               zx_status_t>::type
CloneKernelObject(const zx::object<T>& object, zx::object<T>* result) {
  return object.duplicate(ZX_RIGHT_SAME_RIGHTS, result);
}

template <typename T>
inline typename std::enable_if<!zx::object_traits<T>::supports_duplication,
                               zx_status_t>::type
CloneKernelObject(const zx::object<T>& object, zx::object<T>* result) {
  return ZX_ERR_ACCESS_DENIED;
}

}  // namespace internal
#endif  // __Fuchsia__

// Deep copies the contents of |value| into |result|.
// This operation also attempts to duplicate any handles the value contains.
//
// Returns an error if the value could not be cloned, perhaps because a
// handle was not duplicable.
//
// There are many overloads of this function with the following signature:
//   zx_status_t Clone(const T& value, T* result);
template <typename T>
inline typename std::enable_if<IsPrimitive<T>::value, zx_status_t>::type Clone(
    const T& value, T* result) {
  *result = value;
  return ZX_OK;
}

template <typename T>
inline
#ifdef __Fuchsia__
    typename std::enable_if<!IsPrimitive<T>::value &&
                                !std::is_base_of<zx::object_base, T>::value,
                            zx_status_t>::type
#else   // __Fuchsia__
    typename std::enable_if<!IsPrimitive<T>::value, zx_status_t>::type
#endif  // __Fuchsia__
    Clone(const T& value, T* result) {
  return value.Clone(result);
}

#ifdef __Fuchsia__
template <typename T>
zx_status_t Clone(const zx::object<T>& value, zx::object<T>* result) {
  if (!value) {
    result->reset();
    return ZX_OK;
  }
  return internal::CloneKernelObject(value, result);
}
#endif  // __Fuchsia__

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
inline typename std::enable_if<!IsPrimitive<T>::value, zx_status_t>::type Clone(
    const VectorPtr<T>& value, VectorPtr<T>* result) {
  if (!value) {
    *result = VectorPtr<T>();
    return ZX_OK;
  }
  result->resize(value->size());
  for (size_t i = 0; i < value->size(); ++i) {
    zx_status_t status = Clone(value->at(i), &(*result)->at(i));
    if (status != ZX_OK)
      return status;
  }
  return ZX_OK;
}

template <typename T>
inline typename std::enable_if<IsPrimitive<T>::value, zx_status_t>::type Clone(
    const VectorPtr<T>& value, VectorPtr<T>* result) {
  if (!value) {
    *result = VectorPtr<T>();
    return ZX_OK;
  }
  result->reset(*value);
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

// Returns a deep copy of |value|.
// This operation also attempts to duplicate any handles the value contains.
//
// Crashes the program if the value could not be cloned, perhaps because a
// handle was not duplicable.
template <typename T>
inline T Clone(const T& value) {
  T clone;
  zx_status_t status = Clone(value, &clone);
  ZX_ASSERT(status == ZX_OK);
  return clone;
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_CODING_TRAITS_H_
