// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/socket.h>

#include <vector>

#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "assert.h"

namespace {

// Measure the times taken to enqueue and then dequeue some bytes from a
// Zircon socket, on a single thread.  This does not involve any
// cross-thread wakeups. The |socket_flags| can be used to control whether the
// socket is a stream or a datagram, and |queued| represents how many messages
// to write to the socket before beginning the benchmark. Having messages queued
// on the socket allows for testing scenarios where the socket stays non-empty,
// versus transitions from empty to non-empty and back on each iteration.
bool SocketWriteReadTest(perftest::RepeatState* state, uint32_t socket_flags, uint32_t message_size,
                         uint32_t queued) {
  state->DeclareStep("write");
  state->DeclareStep("read");
  state->SetBytesProcessedPerRun(message_size);

  zx::socket socket1;
  zx::socket socket2;
  ASSERT_OK(zx::socket::create(socket_flags, &socket1, &socket2));
  std::vector<char> buffer(message_size);

  for (uint32_t i = 0; i < queued; i++) {
    size_t bytes_written;
    ASSERT_OK(socket1.write(0, buffer.data(), buffer.size(), &bytes_written));
    ZX_ASSERT(bytes_written == buffer.size());
  }

  while (state->KeepRunning()) {
    size_t bytes_written;
    ASSERT_OK(socket1.write(0, buffer.data(), buffer.size(), &bytes_written));
    ZX_ASSERT(bytes_written == buffer.size());
    state->NextStep();

    size_t bytes_read;
    ASSERT_OK(socket2.read(0, buffer.data(), buffer.size(), &bytes_read));
    ZX_ASSERT(bytes_read == buffer.size());
  }
  return true;
}

void RegisterTests() {
  // Since stream payloads can coexist in different MBufChains internally we want to test cases
  // where messages always sit inside a buffer, will stride across a single buffer boundary, and
  // require multiple buffers.
  static const unsigned kStreamMessageSizesInBytes[] = {
      64,
      1 * 1024,
      64 * 1024,
  };
  // Datagrams always occupy their own MBufChain, so just test a very small and very large message
  // to show baseline costs versus copying overhead.
  static const unsigned kDatagramMessageSizesInBytes[] = {
      64,
      64 * 1024,
  };
  static const unsigned kMessagesToQueue[] = {
      0,
      1,
  };

  for (auto message_size : kStreamMessageSizesInBytes) {
    for (auto queued_messages : kMessagesToQueue) {
      auto name = fbl::StringPrintf("Socket/Stream/WriteRead/%ubytes/%uqueued", message_size,
                                    queued_messages);
      perftest::RegisterTest(name.c_str(), SocketWriteReadTest, ZX_SOCKET_STREAM, message_size,
                             queued_messages);
    }
  }
  for (auto message_size : kDatagramMessageSizesInBytes) {
    for (auto queued_messages : kMessagesToQueue) {
      auto name = fbl::StringPrintf("Socket/Datagram/WriteRead/%ubytes/%uqueued", message_size,
                                    queued_messages);
      perftest::RegisterTest(name.c_str(), SocketWriteReadTest, ZX_SOCKET_DATAGRAM, message_size,
                             queued_messages);
    }
  }
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
