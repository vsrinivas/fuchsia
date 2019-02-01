// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <fbl/string_printf.h>
#include <lib/zx/channel.h>
#include <perftest/perftest.h>

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
  ZX_ASSERT(zx::channel::create(0, &channel1, &channel2) == ZX_OK);
  std::vector<char> buffer(message_size);

  while (state->KeepRunning()) {
    ZX_ASSERT(channel1.write(0, buffer.data(), buffer.size(), nullptr, 0) ==
              ZX_OK);
    state->NextStep();
    ZX_ASSERT(channel2.read(0, buffer.data(), buffer.size(), nullptr, nullptr,
                            0, nullptr) == ZX_OK);
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
