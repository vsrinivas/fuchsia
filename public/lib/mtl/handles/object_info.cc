// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/handles/object_info.h"

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/threads.h>

namespace mtl {

mx_koid_t GetKoid(mx_handle_t handle) {
  mx_info_handle_basic_t info;
  mx_status_t status = mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  return status == NO_ERROR ? info.koid : MX_KOID_INVALID;
}

mx_koid_t GetCurrentProcessKoid() {
  return GetKoid(mx_process_self());
}

mx_koid_t GetCurrentThreadKoid() {
  return GetKoid(thrd_get_mx_handle(thrd_current()));
}

}  // namespace mtl
