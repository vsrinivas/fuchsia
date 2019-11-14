// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>

#include <vector>

#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "assert.h"

namespace {

// Measure the times taken to enqueue and then dequeue a message from a
// Zircon channel, on a single thread.  This does not involve any
// cross-thread wakeups.
bool ChannelWriteReadTest(perftest::RepeatState* state, uint32_t message_size) {
  state->DeclareStep("write");
  state->DeclareStep("read");
  state->SetBytesProcessedPerRun(message_size);

  zx::channel channel1;
  zx::channel channel2;
  ASSERT_OK(zx::channel::create(0, &channel1, &channel2));
  std::vector<char> buffer(message_size);

  while (state->KeepRunning()) {
    ASSERT_OK(channel1.write(0, buffer.data(), buffer.size(), nullptr, 0));
    state->NextStep();
    ASSERT_OK(channel2.read(0, buffer.data(), nullptr, buffer.size(), 0, nullptr, nullptr));
  }
  return true;
}

void RegisterTests() {
  static const unsigned kMessageSizesInBytes[] = {
      64,
      1 * 1024,
      32 * 1024,
      64 * 1024,
  };
  for (auto message_size : kMessageSizesInBytes) {
    auto name = fbl::StringPrintf("Channel/WriteRead/%ubytes", message_size);
    perftest::RegisterTest(name.c_str(), ChannelWriteReadTest, message_size);
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
