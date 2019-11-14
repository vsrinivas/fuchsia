// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/fifo.h>

#include <vector>

#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "assert.h"

namespace {

// Measure the times taken to enqueue and then dequeue some bytes from a
// Zircon fifo, on a single thread.  This does not involve any
// cross-thread wakeups. The number of fifo entries to write/read at a
// time is specified with the |batch_size| argument.
bool FifoWriteReadTest(perftest::RepeatState* state, uint32_t entry_size, uint32_t batch_size) {
  state->DeclareStep("write");
  state->DeclareStep("read");
  state->SetBytesProcessedPerRun(entry_size * batch_size);

  zx::fifo fifo1;
  zx::fifo fifo2;
  ASSERT_OK(zx::fifo::create(PAGE_SIZE / entry_size, entry_size, 0, &fifo1, &fifo2));
  // The buffer represents |batch_size| consecutive entries.
  std::vector<char> buffer(entry_size * batch_size);

  while (state->KeepRunning()) {
    size_t entries_written;
    ASSERT_OK(fifo1.write(entry_size, buffer.data(), batch_size, &entries_written));
    ZX_ASSERT(entries_written == batch_size);
    state->NextStep();

    size_t entries_read;
    ASSERT_OK(fifo2.read(entry_size, buffer.data(), batch_size, &entries_read));
    ZX_ASSERT(entries_read == batch_size);
  }
  return true;
}

struct TestSize {
  // Bytes per entry.
  unsigned entry_size;
  // Entries to write/read per run.
  unsigned batch_size;
};

void RegisterTests() {
  static const TestSize kTestSizes[] = {
      {16, 1}, {16, 4}, {16, 64}, {32, 1}, {32, 4}, {64, 1}, {64, 4},
  };
  for (auto t : kTestSizes) {
    auto name = fbl::StringPrintf("Fifo/WriteRead/%ubytes_%ubatch", t.entry_size, t.batch_size);
    perftest::RegisterTest(name.c_str(), FifoWriteReadTest, t.entry_size, t.batch_size);
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
