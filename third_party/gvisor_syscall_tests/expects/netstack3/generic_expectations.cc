// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  // Netstack3 does not support Unix domain sockets.
  ExpectFailure(tests, "SocketTest.ProtocolUnix");
  ExpectFailure(tests, "SocketTest.UnixSCMRightsOnlyPassedOnce");
  ExpectFailure(tests, "SocketTest.UnixSocketPairProtocol");
  ExpectFailure(tests, "SocketTest.UnixSocketStat");
  ExpectFailure(tests, "SocketTest.UnixSocketStatFS");
  ExpectFailure(tests, "OpenModes/SocketOpenTest.Unix/*");

  // Netstack3 does not support SO_REUSEADDR and only partially supports
  // SO_REUSEPORT for UDP sockets.
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.ReuseAddrDefault/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.SetReuseAddr/*");

  // Skip failures for dual-stack and TCP sockets.
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/4");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/5");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/6");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/7");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/8");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/9");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/10");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/11");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/16");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/17");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/18");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/19");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/20");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/21");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/22");
  SkipTest(tests, "AllIPSockets/TcpUdpSocketPairTest.ShutdownWrFollowedBySendIsError/23");

  // Netstack3 does not support many UDP socket options or operations
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.CheckSkipECN/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.InvalidLargeTOS/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.InvalidNegativeTOS/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.LargeTOSOptionSize/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NegativeTOS/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.SetTOS/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.SmallTOSOptionSize/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.TOSDefault/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.ZeroTOS/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.ZeroTOSOptionSize/*");

  // Expect failure for setting TTL on non-IPv4 sockets.
  for (int i = 2; i <= 7; ++i) {
    ExpectFailure(tests, TestSelector::ParameterizedTest("IPUnboundSockets", "IPUnboundSocketTest",
                                                         "ZeroTtl", absl::StrCat(i)));
    ExpectFailure(tests, TestSelector::ParameterizedTest("IPUnboundSockets", "IPUnboundSocketTest",
                                                         "TtlDefault", absl::StrCat(i)));
    ExpectFailure(tests, TestSelector::ParameterizedTest("IPUnboundSockets", "IPUnboundSocketTest",
                                                         "SetTtl", absl::StrCat(i)));
    ExpectFailure(tests, TestSelector::ParameterizedTest("IPUnboundSockets", "IPUnboundSocketTest",
                                                         "ResetTtlToDefault", absl::StrCat(i)));
  }

  // Skip TCP variants that would otherwise hang forever.
  // TODO(b/245940107): Un-skip these.
  SkipTest(tests, "BlockingIPSockets/BlockingSocketPairTest.RecvBlocks/2");
  SkipTest(tests, "BlockingIPSockets/BlockingSocketPairTest.RecvBlocks/3");
  SkipTest(tests, "BlockingIPSockets/BlockingSocketPairTest.RecvBlocks/4");
  SkipTest(tests, "BlockingIPSockets/BlockingSocketPairTest.RecvBlocks/5");
  SkipTest(tests, "BlockingIPSockets/BlockingSocketPairTest.RecvBlocks/8");
  SkipTest(tests, "BlockingIPSockets/BlockingSocketPairTest.RecvBlocks/9");
  SkipTest(tests, "BlockingIPSockets/BlockingSocketPairTest.RecvBlocks/10");
  SkipTest(tests, "BlockingIPSockets/BlockingSocketPairTest.RecvBlocks/11");

  // Expect failure for TCP sockets.
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/2");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/3");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/6");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/7");

  // Netstack3 does not have complete support for multicast sockets.
  ExpectFailure(tests, "SocketTest.Permission");
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
