// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <zircon/syscalls.h>

#include <trace-provider/provider.h>

#include "util.h"

// Measure the times taken to write and then read back a UDP packet on a
// localhost socket from a single thread using sendto() and recvfrom().
void UDPSendRecv() {
  fbl::unique_fd recvfd;
  FXL_CHECK(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  FXL_CHECK(bind(recvfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) == 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  FXL_CHECK(getsockname(recvfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen) == 0)
      << strerror(errno);
  FXL_CHECK(addrlen == sizeof(addr));
  struct timeval tv = {};
  tv.tv_sec = 1u;
  FXL_CHECK(setsockopt(recvfd.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0)
      << strerror(errno);

  static const uint sizes[] = {64, 1024, 2048, 4096};
  fbl::unique_fd sendfd;
  FXL_CHECK(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  constexpr uint32_t kIterationCount = 1000;
  for (const auto& size : sizes) {
    auto buf = std::make_unique<uint8_t[]>(size);
    // We do not free the strings that we allocate below because the current
    // TRACE infrastructure assumes they are not freed.
    fbl::String* send_event_name = new fbl::String(fbl::StringPrintf("send_%ubytes", size));
    fbl::String* recv_event_name = new fbl::String(fbl::StringPrintf("recv_%ubytes", size));
    for (uint32_t i = 0; i < kIterationCount; i++) {
      TraceSend(sendfd.get(), buf.get(), size, send_event_name->c_str(), &addr, addrlen);
      TraceRecv(recvfd.get(), buf.get(), size, recv_event_name->c_str(), &addr, addrlen);
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
  // TODO(fxb/22911): Replace this sleep with single function that will start
  // a TraceProvider in a non-racy way.
  zx_nanosleep(zx_deadline_after(ZX_SEC(1)));

  UDPSendRecv();

  return 0;
}
