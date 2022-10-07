// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  // Netstack3 does not support TCP (yet).

  // Skip tests that will otherwise hang forever.
  // TODO(b/245940107): un-skip some of these when the data path is ready.
  SkipTest(tests, "AllTCPSockets/*.*/*");
  SkipTest(tests, "BlockingTCPSockets/*.*/*");
  SkipTest(tests, "AllUnixDomainSockets/*.*/*");
  SkipTest(tests, "AllInetTests/SimpleTcpSocketTest.*/*");

  // Skip tests that sometimes crash the Netstack.
  //
  // https://fxbug.dev/111364
  SkipTest(tests, "AllInetTests/TcpSocketTest.NoDelayDefault/*");

  // Otherwise, expect failure.
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.ConnectedAcceptedPeerAndLocalAreReciprocals/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.ConnectOnEstablishedConnection/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.ShutdownWriteInTimeWait/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.ShutdownWriteInFinWait1/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.DataCoalesced/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.SenderAddressIgnored/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.SenderAddressIgnoredOnPeek/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.SendtoAddressIgnored/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.WritevZeroIovec/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.ZeroWriteAllowed/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.NonblockingLargeWrite/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.BlockingLargeWrite/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.LargeSendDontWait/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.NonblockingLargeSend/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.BlockingLargeSend/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.PollWithFullBufferBlocks/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.ClosedWriteBlockingSocket/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.ClosedReadBlockingSocket/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.MsgTrunc/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.MsgTruncWithCtrunc/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.MsgTruncWithCtruncOnly/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.MsgTruncLargeSize/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.MsgTruncPeek/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.NoDelayDefault/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.SetNoDelay/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.TcpInqSetSockOpt/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.TcpInq/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.Tiocinq/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.TcpSCMPriority/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.TimeWaitPollHUP/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.FullBuffer/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.PollAfterShutdown/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.GetSocketAcceptConnListener/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.GetSocketAcceptConnNonListener/*");
  ExpectFailure(tests, "AllInetTests/TcpSocketTest.SendUnblocksOnSendBufferIncrease/*");

  for (const auto& index : {"2", "3", "6", "7"}) {
    // Expect failure for tests that set the TCP_NODELAY socket option.
    ExpectFailure(
        tests, TestSelector::ParameterizedTest("NonBlockingTCPSockets", "NonBlockingSocketPairTest",
                                               "ReadNothingAvailable", index));
    ExpectFailure(
        tests, TestSelector::ParameterizedTest("NonBlockingTCPSockets", "NonBlockingSocketPairTest",
                                               "RecvNothingAvailable", index));
    ExpectFailure(
        tests, TestSelector::ParameterizedTest("NonBlockingTCPSockets", "NonBlockingSocketPairTest",
                                               "RecvMsgNothingAvailable", index));
  }
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
