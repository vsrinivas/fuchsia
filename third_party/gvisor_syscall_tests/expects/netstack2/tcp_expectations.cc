// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/gvisor_syscall_tests/expects/common.h"
#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  AddCommonExpectsTcpNetstack2(tests);
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
  // https://fxbug.dev/20628
  ExpectFailure(tests, "AllTCPSockets/TCPSocketPairTest.MsgTruncMsgPeek/*");
  // https://fxbug.dev/73043
  SkipTest(tests, "AllInetTests/SimpleTcpSocketTest.NonBlockingConnect_PollWrNorm/*");
  // https://fxbug.dev/85279
  SkipTest(tests, "AllInetTests/SimpleTcpSocketTest.ShutdownReadConnectingSocket/*");
  // https://fxbug.dev/85279
  SkipTest(tests, "AllInetTests/SimpleTcpSocketTest.ShutdownWriteConnectingSocket/*");
  // https://fxbug.dev/85279
  SkipTest(tests, "AllInetTests/SimpleTcpSocketTest.ShutdownReadWriteConnectingSocket/*");
}

}  // namespace netstack_syscall_test
