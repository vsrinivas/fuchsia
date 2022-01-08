// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_SCOPED_UTILS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_SCOPED_UTILS_H_

// This file contains a collection of utilities for owning a resource for the duration of a scope.

#include <threads.h>
#include <zircon/compiler.h>

#include <cstdlib>
#include <memory>
#include <mutex>
#include <type_traits>

namespace std {

// A specialization for std::lock_guard<> for use with mtx_t types.
template <>
class __TA_SCOPED_CAPABILITY lock_guard<mtx_t> {
 public:
  using mutex_type = mtx_t;
  __WARN_UNUSED_CONSTRUCTOR explicit lock_guard(mtx_t& m) __TA_ACQUIRE(m) : m(m) { mtx_lock(&m); }
  __WARN_UNUSED_CONSTRUCTOR lock_guard(mtx_t& m, std::adopt_lock_t t) __TA_REQUIRES(m) : m(m) {}
  ~lock_guard() __TA_RELEASE() { mtx_unlock(&m); }
  lock_guard(const lock_guard&) = delete;
  lock_guard& operator=(const lock_guard&) = delete;

 private:
  mutex_type& m;
};

}  // namespace std

namespace wlan::iwlwifi {

// unique_free_ptr is like std::unique_ptr<>, but uses free() to destroy the held object.
template <typename T>
using unique_free_ptr = std::unique_ptr<T, std::integral_constant<decltype(std::free)*, std::free>>;

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_SCOPED_UTILS_H_
