// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  SkipTest(tests, "IPv4Sockets/*.*/*");

  // Netstack3-produced entries for getifaddrs() do not all have interface
  // names.
  SkipTest(tests, "IPv4UDPUnboundSockets/IPv4UDPUnboundExternalNetworkingSocketTest.*/*");

  // Netstack3 does not support SO_REUSEADDR and only partially supports
  // SO_REUSEPORT for UDP sockets.
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

  // Netstack3 does not have complete support for multicast UDP sockets.
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
  ExpectFailure(tests,
                "IPv4UDPSockets/"
                "IPv4UDPUnboundSocketTest.TestTwoSocketsJoinSameMulticastGroup/*");

  // Uncategorized
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
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
