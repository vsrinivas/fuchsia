// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_THREAD_H
#define PLATFORM_THREAD_H

#include <limits.h>  // ensure stdc-predef.h is included for __STDC_NO_THREADS__
#ifndef __STDC_NO_THREADS__
#include <threads.h>
#endif

#include <cstdint>
#include <string>

#include "platform_handle.h"

namespace magma {

// Use std::thread except for ids.
class PlatformThreadId {
 public:
  PlatformThreadId() { SetToCurrent(); }

  uint64_t id() { return id_; }

  void SetToCurrent() { id_ = GetCurrentThreadId(); }

  bool IsCurrent() { return id_ == GetCurrentThreadId(); }

 private:
  static uint64_t GetCurrentThreadId();

  uint64_t id_ = 0;
};

class PlatformThreadHelper {
 public:
  static void SetCurrentThreadName(const std::string& name);
  static std::string GetCurrentThreadName();

  static bool SetRole(void* device_handle, const std::string& role_name);
#ifndef __STDC_NO_THREADS__
  static bool SetThreadRole(void* device_handle, thrd_t thread, const std::string& role_name);
#endif
};

class PlatformProcessHelper {
 public:
  static std::string GetCurrentProcessName();
  static uint64_t GetCurrentProcessId();
};

}  // namespace magma

#endif  // PLATFORM_THREAD_H
