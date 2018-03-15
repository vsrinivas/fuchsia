// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/process_info.h"

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zx/thread.h>

#include "garnet/bin/debug_agent/object_util.h"
#include "garnet/public/lib/fxl/logging.h"

namespace {

debug_ipc::ThreadRecord::State ThreadStateToEnum(uint32_t state) {
  struct Mapping {
    uint32_t int_state;
    debug_ipc::ThreadRecord::State enum_state;
  };
  static const Mapping mappings[] = {
    { ZX_THREAD_STATE_NEW, debug_ipc::ThreadRecord::State::kNew },
    { ZX_THREAD_STATE_RUNNING, debug_ipc::ThreadRecord::State::kRunning },
    { ZX_THREAD_STATE_SUSPENDED, debug_ipc::ThreadRecord::State::kSuspended },
    { ZX_THREAD_STATE_BLOCKED, debug_ipc::ThreadRecord::State::kBlocked },
    { ZX_THREAD_STATE_DYING, debug_ipc::ThreadRecord::State::kDying },
    { ZX_THREAD_STATE_DEAD, debug_ipc::ThreadRecord::State::kDead }
  };

  for (const Mapping& mapping : mappings) {
    if (mapping.int_state == state)
      return mapping.enum_state;
  }
  FXL_NOTREACHED();
  return debug_ipc::ThreadRecord::State::kDead;
}

}  // namespace

zx_status_t GetProcessInfo(zx_handle_t process, zx_info_process* info) {
  return zx_object_get_info(process, ZX_INFO_PROCESS, info,
                            sizeof(zx_info_process), nullptr, nullptr);
}

zx_status_t GetProcessThreads(zx_handle_t process,
                              std::vector<debug_ipc::ThreadRecord>* threads) {
  auto koids = GetChildKoids(process, ZX_INFO_PROCESS_THREADS);
  threads->resize(koids.size());
  for (size_t i = 0; i < koids.size(); i++) {
    (*threads)[i].koid = koids[i];

    zx_handle_t handle;
    if (zx_object_get_child(process, koids[i], ZX_RIGHT_SAME_RIGHTS, &handle)
        == ZX_OK) {
      FillThreadRecord(zx::thread(handle), &(*threads)[i]);
    }
  }
  return ZX_OK;
}

void FillThreadRecord(const zx::thread& thread,
                      debug_ipc::ThreadRecord* record) {
  record->koid = KoidForObject(thread);
  record->name = NameForObject(thread);

  zx_info_thread info;
  if (thread.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr) ==
      ZX_OK) {
    record->state = ThreadStateToEnum(info.state);
  } else {
    FXL_NOTREACHED();
    record->state = debug_ipc::ThreadRecord::State::kDead;
  }
}
