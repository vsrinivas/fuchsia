// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/perftest.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"

namespace {

bool ThreadCreateAndJoinTest() {
  zx_handle_t thread;
  constexpr char tname[] = "test thread";
  FXL_CHECK(zx_thread_create(zx_process_self(), tname, sizeof(tname), 0, &thread) == ZX_OK);

  uint64_t stack_size = 16 * 1024u;
  zx_handle_t stack_vmo;
  FXL_CHECK(zx_vmo_create(stack_size, 0, &stack_vmo) == ZX_OK);

  zx_handle_t vmar = zx_vmar_root_self();
  zx_vaddr_t stack_base;
  uint32_t perm = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
  FXL_CHECK(zx_vmar_map_old(vmar, 0, stack_vmo, 0, stack_size, perm, &stack_base)
            == ZX_OK);

  uintptr_t entry = reinterpret_cast<uintptr_t>(&zx_thread_exit);
  uintptr_t stack = reinterpret_cast<uintptr_t>(stack_base + stack_size);
  FXL_CHECK(zx_thread_start(thread, entry, stack, 0, 0) == ZX_OK);

  zx_signals_t observed;
  FXL_CHECK(zx_object_wait_one(thread, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, &observed) == ZX_OK);

  FXL_CHECK(zx_vmar_unmap(vmar, stack_base, stack_size) == ZX_OK);

  zx_handle_close(thread);
  zx_handle_close(stack_vmo);
  return true;
}

void RegisterTests() {
  perftest::RegisterSimpleTest<ThreadCreateAndJoinTest>("Thread/CreateAndJoin");
}
PERFTEST_CTOR(RegisterTests);

}
