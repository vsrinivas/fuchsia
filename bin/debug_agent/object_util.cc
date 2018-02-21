// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/object_util.h"

#include <zx/process.h>
#include <zx/thread.h>

zx::thread ThreadForKoid(const zx::process& process, zx_koid_t thread_koid) {
  return ThreadForKoid(process.get(), thread_koid);
}

zx::thread ThreadForKoid(zx_handle_t process, zx_koid_t thread_koid) {
  zx_handle_t thread_handle = ZX_HANDLE_INVALID;
  zx_object_get_child(process, thread_koid, ZX_RIGHT_SAME_RIGHTS,
                      &thread_handle);
  return zx::thread(thread_handle);
}

zx_koid_t KoidForProcess(const zx::process& process) {
  zx_info_handle_basic_t info;
  if (process.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr,
                       nullptr) != ZX_OK)
    return 0;
  return info.koid;
}
