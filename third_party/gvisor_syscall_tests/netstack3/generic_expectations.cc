// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expectations.h"

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
  SkipTest(tests, "All/SocketInetReusePortTest.UdpPortReuseMultiThread/*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackTest.NoReusePortFollowingReusePort/TCP");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.ReuseAddrDefault/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.SetReuseAddr/*");

  // Netstack3 does not support dual-stack sockets.
  SkipTest(tests, "All/DualStackSocketTest.AddressOperations/*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackTest.DualStackV6AnyReservesEverything/*");
  ExpectFailure(tests,
                "AllFamilies/SocketMultiProtocolInetLoopbackTest.V4EphemeralPortReserved/"
                "*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackTest.V4MappedAnyOnlyReservesV4/*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackTest.V4MappedEphemeralPortReserved/*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackTest.V4MappedLoopbackOnlyReservesV4/*");
  SkipTest(tests,
           "All/SocketInetReusePortTest.UdpPortReuseMultiThreadShort/"
           "ListenV4Loopback_ConnectV4MappedLoopback");
  SkipTest(tests,
           "All/SocketInetReusePortTest.UdpPortReuseMultiThreadShort/"
           "ListenV6Any_ConnectV4Loopback");

  // Cases fail on TCP.
  SkipTest(tests,
           "AllFamilies/"
           "SocketMultiProtocolInetLoopbackTest."
           "DualStackV6AnyReuseAddrDoesNotReserveV4Any/*");
  SkipTest(tests,
           "AllFamilies/"
           "SocketMultiProtocolInetLoopbackTest."
           "DualStackV6AnyWithListenReservesEverything/*");
  SkipTest(tests,
           "AllFamilies/"
           "SocketMultiProtocolInetLoopbackTest."
           "MultipleBindsAllowedNoListeningReuseAddr/*");
  SkipTest(tests,
           "AllFamilies/"
           "SocketMultiProtocolInetLoopbackTest."
           "DualStackV6AnyReuseAddrListenReservesV4Any/*");

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

  // Netstack3 does not support TCP (yet).

  // Skip tests that will otherwise hang forever.
  // TODO(b/245940107): un-skip some of these when the data path is ready.
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPResetAfterClose/*");
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPAcceptBacklogSizes/*");
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPListenClose/*");
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPListenUnbound/*");
  ExpectFailure(tests, "All/SocketInetLoopbackIsolatedTest.TCPActiveCloseTimeWaitReuseTest/*");
  SkipTest(tests, "All/SocketInetLoopbackIsolatedTest.TCPActiveCloseTimeWaitTest/*");
  SkipTest(tests, "All/SocketInetLoopbackIsolatedTest.TCPPassiveCloseNoTimeWaitTest/*");
  SkipTest(tests, "All/SocketInetLoopbackIsolatedTest.TCPFinWait2Test/*");
  SkipTest(tests, "All/SocketInetLoopbackIsolatedTest.TCPLinger2TimeoutAfterClose/*");

  ExpectFailure(tests, "All/SocketInetLoopbackIsolatedTest.TCPFinWait2Test/*");
  ExpectFailure(tests,
                "All/SocketInetLoopbackIsolatedTest.TCPPassiveCloseNoTimeWaitReuseTest/"
                "*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.AcceptedInheritsTCPUserTimeout/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCP/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPAcceptAfterReset/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPBacklog/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPDeferAccept/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPDeferAcceptTimeout/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPInfoState/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenCloseConnectingRead/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenCloseDuringConnect/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenShutdown/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenShutdownConnectingRead/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenShutdownDuringConnect/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenShutdownListen/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPNonBlockingConnectClose/*");
  ExpectFailure(tests, "All/SocketInetReusePortTest.TcpPortReuseMultiThread/*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackIsolatedTest.BindToDeviceReusePort/TCP");

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

  // Expect failure for TCP sockets.
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/2");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/3");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/6");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/7");

  // Netstack3 does not have complete support for multicast sockets.
  ExpectFailure(tests, "BadSocketPairArgs.ValidateErrForBadCallsToSocketPair");
  ExpectFailure(tests, "SocketInetLoopbackTest.LoopbackAddressRangeConnect");
  ExpectFailure(tests, "SocketTest.Permission");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackIsolatedTest."
                "V4EphemeralPortReservedReuseAddr/*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackIsolatedTest."
                "V4MappedEphemeralPortReservedReuseAddr/*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackIsolatedTest."
                "V6EphemeralPortReservedReuseAddr/*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackTest.PortReuseTwoSockets/TCP");
  ExpectFailure(tests,
                "AllFamilies/SocketMultiProtocolInetLoopbackTest.V6EphemeralPortReserved/"
                "*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackTest.V6OnlyV6AnyReservesV6/*");
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
