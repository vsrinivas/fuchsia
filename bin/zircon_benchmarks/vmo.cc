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

void RegisterTests() {
  for (bool do_write : {false, true}) {
    for (unsigned size_in_kbytes : {128, 1000}) {
      auto name = fbl::StringPrintf(
          "Vmo/%s/%ukbytes", do_write ? "Write" : "Read", size_in_kbytes);
      perftest::RegisterTest(name.c_str(), VmoReadOrWriteTest,
                             size_in_kbytes * 1024, do_write);
    }
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
