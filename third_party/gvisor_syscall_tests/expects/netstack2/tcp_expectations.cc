// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.BasicSendmmsg/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.BasicRecvmmsg/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.RecvmmsgTimeoutBeforeRecv/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.RecvmmsgInvalidTimeout/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.SendmmsgIsLimitedByMAXIOV/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.SendmsgRecvmsg10KB/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.SendmsgRecvmsg16KB/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.SendmsgRecvmsgMsgCtruncNoop/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.RecvmsgMsghdrFlagsCleared/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.RecvmsgPeekMsghdrFlagsCleared/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.RecvWaitAll/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.RecvWaitAllDontWait/*");
  // Fuchsia does not support Unix sockets.
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.RecvTimeoutWaitAll/*");
  // https://fxbug.dev/20628
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.MsgTrunc/*");
  // https://fxbug.dev/20628
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.MsgTruncWithCtrunc/*");
  // https://fxbug.dev/20628
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.MsgTruncWithCtruncOnly/*");
  // https://fxbug.dev/20628
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.MsgTruncLargeSize/*");
  // https://fxbug.dev/20628
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.MsgTruncPeek/*");
  // This test hangs until cl/390312274 is in the Fuchsia SDK.
  SkipTest(tests, "AllInetTests/TcpSocketTest.SendUnblocksOnSendBufferIncrease/*");
  // https://fxbug.dev/41617
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.TcpInqSetSockOpt/*");
  // https://fxbug.dev/41617
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.TcpInq/*");
  // https://fxbug.dev/41617
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.TcpSCMPriority/*");
  // https://fxbug.dev/62744
  // Skip flaky test.
  SkipTest(tests, "AllInetTests/SimpleTcpSocketTest.SelfConnectSendRecv/*");
  // https://fxbug.dev/73028
  SkipTest(tests, "AllTCPSockets/TCPSocketPairTest.RSTCausesPollHUP/*");
  // third_party/gvisor/test/syscalls/linux/socket_ip_tcp_generic.cc:125
  // Value of: RetryEINTR(read)(sockets->first_fd(), buf, sizeof(buf))
  // Expected: -1 (failure), with errno PosixError(errno=104 0)
  //   Actual: 0 (of type long)
  ExpectFailure(tests, "AllTCPSockets/TCPSocketPairTest.RSTSentOnCloseWithUnreadData/*");
  // https://fxbug.dev/73031
  SkipTest(tests,
           "AllTCPSockets/"
           "TCPSocketPairTest.RSTSentOnCloseWithUnreadDataAllowsReadBuffered/*");

  // https://fxbug.dev/73032
  ExpectFailure(tests,
                "AllTCPSockets/"
                "TCPSocketPairTest."
                "ShutdownRdUnreadDataShouldCauseNoPacketsUnlessClosed/*");
  // https://fxbug.dev/70837
  // Skip this test as it flakes often because of reaching file descriptor
  // resource limits on Fuchsia. Bumping up the resource limit in Fuchsia might
  // be more involved.
  SkipTest(tests, "AllTCPSockets/TCPSocketPairTest.TCPResetDuringClose/*");
  // https://fxbug.dev/20628
  ExpectFailure(tests, "AllTCPSockets/TCPSocketPairTest.MsgTruncMsgPeek/*");
  // https://fxbug.dev/46211
  // Deadlock? Test makes no progress even when run in isolation.
  SkipTest(tests, "BlockingTCPSockets/BlockingStreamSocketPairTest.SendMsgTooLarge/*");
  // https://fxbug.dev/42692
  ExpectFailure(tests,
                "BlockingTCPSockets/"
                "BlockingStreamSocketPairTest.RecvLessThanBufferWaitAll/*");
  // https://fxbug.dev/73043
  SkipTest(tests, "AllInetTests/SimpleTcpSocketTest.NonBlockingConnect_PollWrNorm/*");
  // https://fxbug.dev/74836
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.SetAndGetBooleanSocketOptions/*");
  // https://fxbug.dev/85279
  SkipTest(tests, "AllInetTests/SimpleTcpSocketTest.ShutdownReadConnectingSocket/*");
  // https://fxbug.dev/85279
  SkipTest(tests, "AllInetTests/SimpleTcpSocketTest.ShutdownWriteConnectingSocket/*");
  // https://fxbug.dev/85279
  SkipTest(tests, "AllInetTests/SimpleTcpSocketTest.ShutdownReadWriteConnectingSocket/*");
  // https://fxbug.dev/74639
  ExpectFailure(tests, "AllUnixDomainSockets/AllSocketPairTest.GetSetSocketRcvlowatOption/*");
}

}  // namespace netstack_syscall_test
