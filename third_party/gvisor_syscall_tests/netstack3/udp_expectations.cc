// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  // Netstack3 does not support SO_REUSEADDR and only partially supports
  // SO_REUSEPORT for UDP sockets.
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.ReuseAddrDefault/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetReuseAddr/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetReuseAddrReusePort/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetReusePort/*");
  ExpectFailure(tests, "UdpInet6SocketTest.ConnectInet4Sockaddr");

  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/0");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/3");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/4");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/5");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/6");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/8");
  ExpectFailure(tests, "UdpBindTest/SendtoTest.Sendto/9");

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

  // Netstack3 does not have complete support for multicast UDP sockets.
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.MulticastLoopDefault/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetMulticastLoop/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetMulticastLoopChar/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetUDPMulticastTTLMin/*");
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
  // https://fxbug.dev/74639
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.GetSetSocketRcvlowatOption/*");
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
