// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/handle.h>
#include <lib/zx/process.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <perftest/perftest.h>

#include "assert.h"

namespace {

// See fdio_spawn_helper.cc.
// Note: while being both a "shell binary" and a "test component", this
// execution path results in benchmarking shell binary resolution as well as
// fdio_spawn.
constexpr const char* kPath = "/bin/fdio_spawn_helper";
constexpr const char* const kArgv[]{kPath, nullptr};

// Benchmark |fdio_spawn| by spawning a process that simply exits.
bool SpawnTest(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
    zx::handle process;
    ASSERT_OK(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_DEFAULT_LDSVC, kPath, kArgv,
                         process.reset_and_get_address()));
    ASSERT_OK(process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));
    zx_info_process_t info;
    ASSERT_OK(process.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr));
    ZX_ASSERT(info.exited);
    ZX_ASSERT(info.return_code == 0);
  }
  return true;
}

void RegisterTests() { perftest::RegisterTest("Fdio/Spawn", SpawnTest); }
PERFTEST_CTOR(RegisterTests)

}  // namespace
