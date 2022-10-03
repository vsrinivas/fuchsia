// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  // https://fxbug.dev/46102
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.SetAndReceiveIPPKTINFO/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.IpMulticastIPPacketInfo/*");
  // Attempts to exhaust ephemeral sockets (65k), but Fuchsia allows only 1k
  // FDs.
  //
  // https://fuchsia.googlesource.com/fuchsia/+/a7a1b55/zircon/system/ulib/fdio/include/lib/fdio/limits.h#13
  //
  // https://fxbug.dev/33737
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketNogotsanTest.UDPBindPortExhaustion/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketNogotsanTest.UDPConnectPortExhaustion/*");

  // https://fxbug.dev/67016
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.SetAndReceiveIPReceiveOrigDstAddr/*");
  ExpectFailure(tests,
                "IPv6UDPSockets/"
                "IPv6UDPUnboundSocketTest.SetAndReceiveIPReceiveOrigDstAddr/*");
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
