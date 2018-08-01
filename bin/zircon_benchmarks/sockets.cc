// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <fbl/string_printf.h>
#include <lib/zx/socket.h>
#include <perftest/perftest.h>

namespace {

// Measure the times taken to enqueue and then dequeue some bytes from a
// Zircon socket, on a single thread.  This does not involve any
// cross-thread wakeups.
bool SocketWriteReadTest(perftest::RepeatState* state, uint32_t message_size) {
  state->DeclareStep("write");
  state->DeclareStep("read");
  state->SetBytesProcessedPerRun(message_size);

  zx::socket socket1;
  zx::socket socket2;
  ZX_ASSERT(zx::socket::create(ZX_SOCKET_STREAM, &socket1, &socket2) == ZX_OK);
  std::vector<char> buffer(message_size);

  while (state->KeepRunning()) {
    size_t bytes_written;
    ZX_ASSERT(socket1.write(0, buffer.data(), buffer.size(), &bytes_written)
              == ZX_OK);
    ZX_ASSERT(bytes_written == buffer.size());
    state->NextStep();

    size_t bytes_read;
    ZX_ASSERT(socket2.read(0, buffer.data(), buffer.size(), &bytes_read)
              == ZX_OK);
    ZX_ASSERT(bytes_read == buffer.size());
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
    auto name = fbl::StringPrintf("Socket/WriteRead/%ubytes", message_size);
    perftest::RegisterTest(name.c_str(), SocketWriteReadTest, message_size);
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
