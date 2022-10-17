// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expects/common.h"
#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  FilterTestsForLoopbackTcpBacklogTarget(tests);

  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPBacklog/*");

  // Dual-stack TCP sockets are not supported.
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4Any_ConnectV4MappedAny");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4Any_ConnectV4MappedAny");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4Any_ConnectV4MappedLoopback");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4Any_ConnectV4MappedLoopback");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4Loopback_ConnectV4MappedLoopback");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4Loopback_ConnectV4MappedLoopback");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4MappedAny_ConnectV4Any");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4MappedAny_ConnectV4Loopback");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4MappedAny_ConnectV4MappedAny");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4MappedAny_ConnectV4MappedLoopback");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4MappedLoopback_ConnectV4Any");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4MappedLoopback_ConnectV4Loopback");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4MappedLoopback_ConnectV4MappedLoopback");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV6Any_ConnectV4Any");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV6Any_ConnectV4MappedAny");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV6Any_ConnectV4MappedLoopback");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV6Any_ConnectV4Loopback");

  // Netstack3 does not yet follow the Linux/BSD convention that connecting to
  // the unspecified address is equivalent to connecting to loopback.
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4Any_ConnectV4Any");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV4Loopback_ConnectV4Any");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV6Any_ConnectV6Any");
  ExpectFailure(tests,
                "All/SocketInetLoopbackTest.TCPBacklogAcceptAll/"
                "ListenV6Loopback_ConnectV6Any");
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
