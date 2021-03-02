// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_FUNCTIONAL_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_FUNCTIONAL_H_

#include "../type_traits.h"

namespace cpp17 {
namespace internal {

// Definitions for the invoke functions in internal/type_traits.h.
// These collectively implement INVOKE from [func.require] ¶ 1.
template <typename MemFn, typename Class, typename T, typename... Args>
constexpr auto invoke(MemFn Class::*f, T&& obj, Args&&... args)
    -> std::enable_if_t<invoke_pmf_base<MemFn, Class, T>,
                        decltype((std::forward<T>(obj).*f)(std::forward<Args>(args)...))> {
  return (std::forward<T>(obj).*f)(std::forward<Args>(args)...);
}

template <typename MemFn, typename Class, typename T, typename... Args>
constexpr auto invoke(MemFn Class::*f, T&& obj, Args&&... args)
    -> std::enable_if_t<invoke_pmf_refwrap<MemFn, Class, T>,
                        decltype((obj.get().*f)(std::forward<Args>(args)...))> {
  return (obj.get().*f)(std::forward<Args>(args)...);
}

template <typename MemFn, typename Class, typename T, typename... Args>
constexpr auto invoke(MemFn Class::*f, T&& obj, Args&&... args)
    -> std::enable_if_t<invoke_pmf_other<MemFn, Class, T>,
                        decltype(((*std::forward<T>(obj)).*f)(std::forward<Args>(args)...))> {
  return (*std::forward<T>(obj).*f)(std::forward<Args>(args)...);
}

template <typename MemObj, typename Class, typename T>
constexpr auto invoke(MemObj Class::*f, T&& obj)
    -> std::enable_if_t<invoke_pmd_base<MemObj, Class, T>, decltype(std::forward<T>(obj).*f)> {
  return std::forward<T>(obj).*f;
}

template <typename MemObj, typename Class, typename T>
constexpr auto invoke(MemObj Class::*f, T&& obj)
    -> std::enable_if_t<invoke_pmd_refwrap<MemObj, Class, T>, decltype(obj.get().*f)> {
  return obj.get().*f;
}

template <typename MemObj, typename Class, typename T>
constexpr auto invoke(MemObj Class::*f, T&& obj)
    -> std::enable_if_t<invoke_pmd_other<MemObj, Class, T>, decltype((*std::forward<T>(obj)).*f)> {
  return (*std::forward<T>(obj)).*f;
}

template <typename F, typename... Args>
constexpr auto invoke(F&& f, Args&&... args)
    -> decltype(std::forward<F>(f)(std::forward<Args>(args)...)) {
  return std::forward<F>(f)(std::forward<Args>(args)...);
}

}  // namespace internal
}  // namespace cpp17

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_FUNCTIONAL_H_
