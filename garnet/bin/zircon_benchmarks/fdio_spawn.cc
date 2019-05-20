// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/handle.h>
#include <lib/zx/process.h>
#include <perftest/perftest.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace {

// See fdio_spawn_helper.cc.
constexpr const char* kPath =
    "/pkgfs/packages/zircon_benchmarks/0/test/fdio_spawn_helper";
constexpr const char* const kArgv[]{kPath, nullptr};

// Benchmark |fdio_spawn| by spawning a process that simply exits.
bool SpawnTest(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
    zx::handle process;
    ZX_ASSERT(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_DEFAULT_LDSVC, kPath,
                         kArgv, process.reset_and_get_address()) == ZX_OK);
    ZX_ASSERT(process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(),
                               nullptr) == ZX_OK);
    zx_info_process_t info;
    ZX_ASSERT(process.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr,
                               nullptr) == ZX_OK);
    ZX_ASSERT(info.exited);
    ZX_ASSERT(info.return_code == 0);
  }
  return true;
}

void RegisterTests() { perftest::RegisterTest("Fdio/Spawn", SpawnTest); }
PERFTEST_CTOR(RegisterTests)

}  // namespace
