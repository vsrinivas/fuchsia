// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/gvisor_syscall_tests/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "SocketTest.ProtocolUnix");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "SocketTest.UnixSocketPairProtocol");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "SocketTest.UnixSocketStat");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "SocketTest.UnixSocketStatFS");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "SocketTest.UnixSCMRightsOnlyPassedOnce");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "SocketTest.Permission");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "OpenModes/SocketOpenTest.Unix/*");

  // https://fxbug.dev/35593
  ExpectFailure(tests, "BadSocketPairArgs.ValidateErrForBadCallsToSocketPair");
  // https://fxbug.dev/61714
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenShutdownListen/*");
  // https://fxbug.dev/35596
  // Deadlock? Test makes no progress even when run in isolation.
  SkipTest(tests, "All/SocketInetReusePortTest.TcpPortReuseMultiThread/*");
  // https://fxbug.dev/35596
  // Deadlock? Test makes no progress even when run in isolation.
  SkipTest(tests, "All/SocketInetReusePortTest.UdpPortReuseMultiThreadShort/*");
  // https://fxbug.dev/35596
  // Deadlock? Test makes no progress even when run in isolation.
  SkipTest(tests, "All/SocketInetReusePortTest.UdpPortReuseMultiThread/*");
  // https://fxbug.dev/44151
  for (const auto& parameter :
       {"V4AnyBindConnectSendTo", "V4AnyBindSendToConnect", "V4AnyConnectBindSendTo",
        "V4AnyConnectSendToBind", "V4AnySendToBindConnect", "V4AnySendToConnectBind",
        "V4LoopbackBindConnectSendTo", "V4LoopbackBindSendToConnect"}) {
    ExpectFailure(tests, TestSelector::ParameterizedTest("All", "DualStackSocketTest",
                                                         "AddressOperations", parameter));
  }
  // https://fxbug.dev/55205
  //
  // This test encodes some known incorrect behavior on gVisor. That incorrect
  // assertion code path is also taken on Fuchsia, but Fuchsia doesn't have the
  // same bug.
  //
  // Our infrastructure here can't deal with "partial" passes, so we have no
  // choice but to skip this test.
  SkipTest(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/*");
  // https://fxbug.dev/45778
  //
  // [ RUN      ]
  // AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/11
  // Testing with non-blocking connected dual stack TCP socket
  // third_party/gvisor/test/syscalls/linux/socket_ip_tcp_udp_generic.cc:41:
  // Failure Value of: shutdown(sockets->first_fd(), 1) Expected: not -1
  // (success)
  //   Actual: -1 (of type int), with errno PosixError(errno=32 0)
  //
  // [ RUN      ]
  // AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/23
  // Testing with reversed non-blocking connected dual stack TCP socket
  // [       OK ]
  // AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/23 (4 ms)
  //
  // Likely caused by being unable to shut down listening sockets. Possible fix
  // in https://fxrev.dev/437660.
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/*");
}

}  // namespace netstack_syscall_test
