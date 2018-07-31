// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <fbl/string_printf.h>
#include <lib/zx/vmo.h>
#include <perftest/perftest.h>

namespace {

// Measure the time taken to write or read a chunk of data to/from a VMO
// using the zx_vmo_write() or zx_vmo_read() syscalls respectively.
bool VmoReadOrWriteTest(perftest::RepeatState* state, uint32_t copy_size,
                        bool do_write) {
  state->SetBytesProcessedPerRun(copy_size);

  zx::vmo vmo;
  ZX_ASSERT(zx::vmo::create(copy_size, 0, &vmo) == ZX_OK);
  std::vector<char> buffer(copy_size);

  if (do_write) {
    while (state->KeepRunning()) {
      ZX_ASSERT(vmo.write(buffer.data(), 0, copy_size) == ZX_OK);
    }
  } else {
    while (state->KeepRunning()) {
      ZX_ASSERT(vmo.read(buffer.data(), 0, copy_size) == ZX_OK);
    }
  }
  return true;
}

// Measure the time taken to clone a vmo and destroy it.
bool VmoCloneTest(perftest::RepeatState* state, uint32_t copy_size) {
  state->DeclareStep("clone");
  state->DeclareStep("close");
  zx::vmo vmo;
  ZX_ASSERT(zx::vmo::create(copy_size, 0, &vmo) == ZX_OK);

  while (state->KeepRunning()) {
    zx::vmo clone;
    ZX_ASSERT(vmo.clone(ZX_VMO_CLONE_COPY_ON_WRITE, 0, copy_size, &clone) ==
              ZX_OK);
    state->NextStep();
  }

  return true;
}

// Measure the time it takes to clone a vmo, read or write into/from it and
// destroy it.
bool VmoCloneReadOrWriteTest(perftest::RepeatState* state, uint32_t copy_size,
                             bool do_write) {
  state->DeclareStep("clone");
  state->DeclareStep(do_write ? "write" : "read");
  state->DeclareStep("close");
  state->SetBytesProcessedPerRun(copy_size);
  zx::vmo vmo;
  ZX_ASSERT(zx::vmo::create(copy_size, 0, &vmo) == ZX_OK);
  std::vector<char> buffer(copy_size);

  if (do_write) {
    while (state->KeepRunning()) {
      zx::vmo clone;
      ZX_ASSERT(vmo.clone(ZX_VMO_CLONE_COPY_ON_WRITE, 0, copy_size, &clone) ==
                ZX_OK);
      state->NextStep();
      ZX_ASSERT(vmo.write(buffer.data(), 0, copy_size) == ZX_OK);
      state->NextStep();
    }
  } else {
    while (state->KeepRunning()) {
      zx::vmo clone;
      ZX_ASSERT(vmo.clone(ZX_VMO_CLONE_COPY_ON_WRITE, 0, copy_size, &clone) ==
                ZX_OK);
      state->NextStep();
      ZX_ASSERT(vmo.read(buffer.data(), 0, copy_size) == ZX_OK);
      state->NextStep();
    }
  }

  return true;
}

void RegisterTests() {
  for (unsigned size_in_kbytes : {128, 1000}) {
    for (bool do_write : {false, true}) {
      // Read/Write.
      const char* rw = do_write ? "Write" : "Read";
      auto rw_name = fbl::StringPrintf("Vmo/%s/%ukbytes", rw, size_in_kbytes);
      perftest::RegisterTest(rw_name.c_str(), VmoReadOrWriteTest,
                             size_in_kbytes * 1024, do_write);
    }

    // Clone (only run it once).
    auto clone_name = fbl::StringPrintf("Vmo/Clone/%ukbytes", size_in_kbytes);
    perftest::RegisterTest(clone_name.c_str(), VmoCloneTest,
                           size_in_kbytes * 1024);

    for (bool do_write : {false, true}) {
      // Clone Read/Write.
      const char* rw = do_write ? "Write" : "Read";
      auto clone_rw_name =
          fbl::StringPrintf("Vmo/Clone%s/%ukbytes", rw, size_in_kbytes);
      perftest::RegisterTest(clone_rw_name.c_str(), VmoCloneReadOrWriteTest,
                             size_in_kbytes * 1024, do_write);
    }
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
