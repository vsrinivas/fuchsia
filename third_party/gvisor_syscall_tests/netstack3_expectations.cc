#include <string>

#include "expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  // Netstack3 does not support Unix domain sockets.
  ExpectFailure(tests, "SocketTest.ProtocolUnix");
  ExpectFailure(tests, "SocketTest.UnixSCMRightsOnlyPassedOnce");
  ExpectFailure(tests, "SocketTest.UnixSocketPairProtocol");
  ExpectFailure(tests, "SocketTest.UnixSocketStat");
  ExpectFailure(tests, "SocketTest.UnixSocketStatFS");
  ExpectFailure(tests, "OpenModes/SocketOpenTest.Unix/*");

  // TODO(b/243164162): Stop skipping these manually once NS3 is returning EPERM
  // for raw and packet socket creation.
  SkipTest(tests, "AllInetTests/RawPacketTest.*/*");
  SkipTest(tests, "AllInetTests/CookedPacketTest.*/*");
  SkipTest(tests, "AllPacketSocketTests/*.*/*");
  SkipTest(tests, "AllRawPacketMsgSizeTest/*.*/*");
  SkipTest(tests, "BasicCookedPacketTest.WrongType");
  SkipTest(tests, "RawHDRINCL.*");
  SkipTest(tests, "RawSocketICMPTest.*");
  SkipTest(tests, "RawSocketICMPv6Test.*");
  SkipTest(tests, "AllInetTests/RawSocketTest.*/*");
  SkipTest(tests, "AllRawSocketTests/*.*/*");
  SkipTest(tests, "RawSocketTest.*");
  SkipTest(tests, "IPv4Sockets/*.*/*");

  // Netstack3-produced entries for getifaddrs() do not all have interface
  // names.
  SkipTest(tests, "IPv4UDPUnboundSockets/IPv4UDPUnboundExternalNetworkingSocketTest.*/*");

  // Netstack3 does not support SO_REUSEADDR and only partially supports
  // SO_REUSEPORT for UDP sockets.
  SkipTest(tests, "All/SocketInetReusePortTest.UdpPortReuseMultiThread/*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackTest.NoReusePortFollowingReusePort/TCP");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.ReuseAddrDefault/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetReuseAddr/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetReuseAddrReusePort/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.ReuseAddrDefault/*");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.SetReuseAddr/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.BindDoubleReuseAddrReusePortThenReuseAddr/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.BindDoubleReuseAddrReusePortThenReusePort/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.BindReuseAddrReusePortConversionReversable1/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.BindReuseAddrReusePortConversionReversable2/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest."
                "BindReuseAddrReusePortConvertibleToReuseAddr/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest."
                "BindReuseAddrReusePortConvertibleToReusePort/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.BindReuseAddrThenReusePort/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.BindReusePortThenReuseAddr/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.ReuseAddrDistribution/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.ReuseAddrReusePortDistribution/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetReusePort/*");
  ExpectFailure(tests, "UdpInet6SocketTest.ConnectInet4Sockaddr");

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
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/0");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/3");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/4");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/5");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/6");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/8");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/9");
  SkipTest(tests,
           "All/SocketInetReusePortTest.UdpPortReuseMultiThreadShort/"
           "ListenV4Loopback_ConnectV4MappedLoopback");
  SkipTest(tests,
           "All/SocketInetReusePortTest.UdpPortReuseMultiThreadShort/"
           "ListenV6Any_ConnectV4Loopback");

  // Cases are either no-ops (for UDP) or fail (for TCP). Skip them here since
  // the tests that are no-ops otherwise pass and make expectations more
  // verbose.
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

  // Skip failures for dual-stack and TCP sockets but not UDP sockets.
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

  // Skip this test that hangs. The test runs the failing syscall in a separate
  // thread and so can't be trivially modified to abort.
  SkipTest(tests, "AllInetTests/UdpSocketTest.SynchronousReceive/*");

  // Cases are either skipped entirely (0, 1) or fail (2)
  SkipTest(tests, "AllInetTests/UdpSocketControlMessagesTest.SendAndReceiveTOSorTClass/*");

  ExpectFailure(tests, "AllInetTests/UdpSocketTest.ConnectAndSendNoReceiver/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.ConnectToZeroPortBound/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.ConnectToZeroPortConnected/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.ConnectToZeroPortUnbound/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.ConnectWriteToInvalidPort/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.DisconnectAfterBindToAny/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.DisconnectAfterBindToUnspecAndConnect/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.DisconnectAfterConnectAnyWithPort/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.DisconnectAfterConnectWithoutBind/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.FIONREADShutdown/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.FIONREADWriteShutdown/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.FIONREADZeroLengthPacket/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.FIONREADZeroLengthWriteShutdown/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.Fionread/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.Getpeername/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.Getsockname/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.ReadShutdownNonblockPendingData/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.RecvBufLimits/*");
  ExpectFailure(tests,
                "AllInetTests/"
                "UdpSocketTest.SendPacketLargerThanSendBufOnNonBlockingSocket/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.SendToAddressOtherThanConnected/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.SoNoCheck/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.SoNoCheckOffByDefault/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.SoTimestamp/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.SoTimestampOffByDefault/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.TimestampIoctl/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.TimestampIoctlNothingRead/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.TimestampIoctlPersistence/*");

  // Netstack3 does not support many UDP socket options or operations
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.BasicRecvmmsg/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.BasicSendmmsg/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.GetSndBufSucceeds/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.GetSocketOutOfBandInlineOption/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.GetSocketRcvbufOption/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.KeepAliveSocketOption/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.LingerSocketOption/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.RecvmmsgInvalidTimeout/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.RecvmmsgTimeoutBeforeRecv/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.RecvmsgMsghdrFlagsCleared/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.SendmmsgIsLimitedByMAXIOV/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.RecvmsgPeekMsghdrFlagsCleared/*");
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.SetAndGetBooleanSocketOptions/*");
  ExpectFailure(tests, "AllUDPSockets/NonStreamSocketPairTest.SendMsgTooLarge/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.GetSocketAcceptConn/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.IPPKTINFODefault/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.RecvTosDefault/*");
  ExpectFailure(tests,
                "AllInetTests/"
                "UdpSocketControlMessagesTest.SendAndReceiveTTLOrHopLimit/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketControlMessagesTest.SetAndReceivePktInfo/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketControlMessagesTest.SetAndReceiveTOSOrTClass/*");
  ExpectFailure(tests, "AllInetTests/UdpSocketControlMessagesTest.SetAndReceiveTTLOrHopLimit/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.TOSRecvMismatch/*");
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

  // Expect failure for dual-stack UDP sockets.
  for (const auto& index : {"2", "5"}) {
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "BasicReadWrite", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "BasicReadWriteBadBuffer", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "BasicSendRecv", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "GetSockoptType", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvTimeoutDefault", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvTimeoutNegSecRead", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvTimeoutNegSecRecv", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvTimeoutNegSecRecvmsg", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvTimeoutReadSucceeds", index));
    ExpectFailure(tests,
                  TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                  "RecvTimeoutRecvOneSecondSucceeds", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvTimeoutRecvSucceeds", index));
    ExpectFailure(tests,
                  TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                  "RecvTimeoutRecvmsgOneSecondSucceeds", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvTimeoutRecvmsgSucceeds", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvTimeoutUsecNeg", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvTimeoutUsecTooLarge", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvTimeoutWaitAll", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvWaitAll", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvWaitAllDontWait", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RecvmsgIovNotUpdated", index));
    ExpectFailure(tests,
                  TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                  "RecvmsgMsghdrFlagsNotClearedOnFailure", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SendTimeoutAllowsSend", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SendTimeoutAllowsSendmsg", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SendTimeoutAllowsWrite", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SendTimeoutDefault", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SendTimeoutUsecNeg", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SendTimeoutUsecTooLarge", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SendmsgRecvmsg10KB", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SendmsgRecvmsg16KB", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SendmsgRecvmsgMsgCtruncNoop", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SetGetRecvTimeout", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SetGetRecvTimeoutLargerArg", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SetGetSendTimeout", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "SetGetSendTimeoutLargerArg", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "NonStreamSocketPairTest",
                                                         "MsgTruncNotFull", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "NonStreamSocketPairTest",
                                                         "MsgTruncSameSize", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "NonStreamSocketPairTest",
                                                         "MsgTruncTruncation", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest(
                             "AllUDPSockets", "NonStreamSocketPairTest",
                             "MsgTruncTruncationRecvmsgMsghdrFlagMsgTrunc", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "NonStreamSocketPairTest",
                                                         "RecvmsgMsgTruncZeroLen", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "NonStreamSocketPairTest",
                                                         "RecvmsgMsghdrFlagMsgTrunc", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "NonStreamSocketPairTest",
                                                         "RecvmsgTruncPeekDontwaitZeroLen", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "NonStreamSocketPairTest",
                                                         "SingleRecv", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "NonStreamSocketPairTest",
                                                         "SplitRecv", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "UDPSocketPairTest",
                                                         "SetUDPMulticastTTLAboveMax", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "UDPSocketPairTest",
                                                         "SetUDPMulticastTTLBelowMin", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "RcvBufSucceeds", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "GetSockoptDomain", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "GetSockoptProtocol", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "UDPSocketPairTest",
                                                         "ReusePortDefault", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "AllSocketPairTest",
                                                         "MsgPeek", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "NonStreamSocketPairTest",
                                                         "SinglePeek", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "NonStreamSocketPairTest",
                                                         "RecvmsgMsgTruncMsgPeekZeroLen", index));
  }

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

  // Expect failure for setting TTL on non-IPv4 sockets.
  for (const auto& index : {"0", "2", "3", "5"}) {
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "UDPSocketPairTest",
                                                         "MulticastTTLDefault", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "UDPSocketPairTest",
                                                         "SetUDPMulticastTTLNegativeOne", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "UDPSocketPairTest",
                                                         "SetUDPMulticastTTLChar", index));
    ExpectFailure(tests, TestSelector::ParameterizedTest("AllUDPSockets", "UDPSocketPairTest",
                                                         "SetUDPMulticastTTLMax", index));
  }

  // Netstack3 does not support TCP (yet).

  // Skip tests that will otherwise hang forever.
  // TODO(b/245940107): un-skip some of these when the data path is ready.
  SkipTest(tests, "AllTCPSockets/*.*/*");
  SkipTest(tests, "BlockingTCPSockets/*.*/*");
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPResetAfterClose/*");
  SkipTest(tests, "AllUnixDomainSockets/*.*/*");
  SkipTest(tests, "AllInetTests/SimpleTcpSocketTest.*/*");
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPAcceptBacklogSizes/*");
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPListenClose/*");
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPListenUnbound/*");
  ExpectFailure(tests, "All/SocketInetLoopbackIsolatedTest.TCPActiveCloseTimeWaitReuseTest/*");
  SkipTest(tests, "All/SocketInetLoopbackIsolatedTest.TCPActiveCloseTimeWaitTest/*");
  SkipTest(tests, "All/SocketInetLoopbackIsolatedTest.TCPPassiveCloseNoTimeWaitTest/*");
  SkipTest(tests, "All/SocketInetLoopbackIsolatedTest.TCPFinWait2Test/*");
  SkipTest(tests, "All/SocketInetLoopbackIsolatedTest.TCPLinger2TimeoutAfterClose/*");

  // Otherwise expect failure so we get a signal when they start passing.
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

  ExpectFailure(tests, "AllInetTests/TcpSocketTest.*/*");
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

  // Expect failure for non-blocking UDP sockets, and all TCP sockets.
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/2");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/3");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/6");
  ExpectFailure(tests, "IPUnboundSockets/IPUnboundSocketTest.NullTOS/7");

  // Netstack3 does not have complete support for multicast UDP sockets.
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.MulticastLoopDefault/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.IpMulticastIPPacketInfo/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.IpMulticastLoopbackAddrNoDefaultSendIf/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.IpMulticastLoopbackIfAddr/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.IpMulticastLoopbackIfAddrConnect/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.IpMulticastLoopbackIfAddrSelf/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.IpMulticastLoopbackIfAddrSelfConnect/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.IpMulticastLoopbackIfAddrSelfNoLoop/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.IpMulticastLoopbackIfNic/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.IpMulticastLoopbackIfNicConnect/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.IpMulticastLoopbackIfNicSelf/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.IpMulticastLoopbackIfNicSelfConnect/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.IpMulticastLoopbackIfNicSelfNoLoop/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.IpMulticastLoopbackNicNoDefaultSendIf/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetMulticastLoop/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetMulticastLoopChar/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetUDPMulticastTTLMin/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.TestTwoSocketsJoinSameMulticastGroup/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetEmptyIPAddMembership/0");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetEmptyIPAddMembership/2");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetEmptyIPAddMembership/3");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetEmptyIPAddMembership/5");

  // Uncategorized
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.ReceiveOrigDstAddrDefault/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetAndGetIPPKTINFO/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetAndGetReceiveOrigDstAddr/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetAndGetSocketLinger/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetRecvTos/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketNogotsanTest.UDPBindPortExhaustion/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketNogotsanTest.UDPConnectPortExhaustion/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.SetAndReceiveIPPKTINFO/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.SetAndReceiveIPReceiveOrigDstAddr/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.SetSocketRecvBuf/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.SetSocketSendBuf/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.SetSocketSendBufAboveMax/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.SetSocketSendBufBelowMin/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.TestBindToBcastThenReceive/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.TestBindToBcastThenSend/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.TestBindToMcastThenJoinThenReceive/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.TestBindToMcastThenNoJoinThenNoReceive/*");
  ExpectFailure(tests, "IPv4UDPSockets/IPv4UDPUnboundSocketTest.TestBindToMcastThenSend/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.TestMcastReceptionOnTwoSockets/*");
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.TestMcastReceptionWhenDroppingMemberships/*");
  ExpectFailure(tests, "IPv6UDPSockets/IPv6UDPUnboundSocketTest.IPv6PacketInfo/*");
  ExpectFailure(tests,
                "IPv6UDPSockets/"
                "IPv6UDPUnboundSocketTest.SetAndReceiveIPReceiveOrigDstAddr/*");
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
  // https://fxbug.dev/74639
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.GetSetSocketRcvlowatOption/*");
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
