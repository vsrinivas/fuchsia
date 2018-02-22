// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/system_info.h"

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include "garnet/bin/debug_agent/object_util.h"

zx_status_t GetProcessThreads(zx_handle_t process,
                              std::vector<debug_ipc::ThreadRecord>* threads) {
  auto koids = GetChildKoids(process, ZX_INFO_PROCESS_THREADS);
  threads->resize(koids.size());
  for (size_t i = 0; i < koids.size(); i++) {
    (*threads)[i].koid = koids[i];

    zx_handle_t handle;
    if (zx_object_get_child(process, koids[i], ZX_RIGHT_SAME_RIGHTS, &handle) == ZX_OK) {
      (*threads)[i].name = NameForObject(handle);
      zx_handle_close(handle);
    }
  }
  return ZX_OK;
}
