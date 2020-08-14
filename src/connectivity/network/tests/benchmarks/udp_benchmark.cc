// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/provider.h>
#include <zircon/syscalls.h>

#include <array>

#include "util.h"

// Measure the times taken to write and then read back a UDP packet on a
// localhost socket from a single thread using sendto() and recvfrom().
void UDPSendRecv() {
  fbl::unique_fd recvfd;
  FX_CHECK(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  FX_CHECK(bind(recvfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) == 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  FX_CHECK(getsockname(recvfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen) == 0)
      << strerror(errno);
  FX_CHECK(addrlen == sizeof(addr));
  struct timeval tv = {};
  tv.tv_sec = 1u;
  FX_CHECK(setsockopt(recvfd.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0)
      << strerror(errno);

  struct SizeAndTraceEventNames {
    ssize_t size;
    const char* send_event_name;
    const char* recv_event_name;
  };

#define CREATE_SIZE_AND_TRACE_EVENT_NAMES(size) \
  { size, "send_" #size "bytes", "recv_" #size "bytes" }
  constexpr std::array<SizeAndTraceEventNames, 4> kSizeAndTraceEventNames = {{
      CREATE_SIZE_AND_TRACE_EVENT_NAMES(64),
      CREATE_SIZE_AND_TRACE_EVENT_NAMES(1024),
      CREATE_SIZE_AND_TRACE_EVENT_NAMES(2048),
      CREATE_SIZE_AND_TRACE_EVENT_NAMES(4096),
  }};

  fbl::unique_fd sendfd;
  FX_CHECK(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  constexpr uint32_t kIterationCount = 1000;
  for (const auto& [size, send_event_name, recv_event_name] : kSizeAndTraceEventNames) {
    auto buf = std::make_unique<uint8_t[]>(size);
    for (uint32_t i = 0; i < kIterationCount; i++) {
      TraceSend(sendfd.get(), buf.get(), size, send_event_name, &addr, addrlen);
      TraceRecv(recvfd.get(), buf.get(), size, recv_event_name, &addr, addrlen);
    }
  }
}

int main(int argc, char* argv[]) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("trace");
  trace::TraceProviderWithFdio provider(loop.dispatcher());

  // Wait for tracing to get set up.  Without this, the tracing system can miss
  // some of the initial tracing events we generate later.
  //
  // TODO(fxbug.dev/22911): Replace this sleep with single function that will start
  // a TraceProvider in a non-racy way.
  zx_nanosleep(zx_deadline_after(ZX_SEC(1)));

  UDPSendRecv();

  return 0;
}
