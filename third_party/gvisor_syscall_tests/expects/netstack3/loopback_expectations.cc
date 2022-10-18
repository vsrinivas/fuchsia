// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expects/common.h"
#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  FilterTestsForLoopbackTarget(tests);

  // Netstack3 does not have complete support for multicast sockets.
  ExpectFailure(tests, "BadSocketPairArgs.ValidateErrForBadCallsToSocketPair");
  ExpectFailure(tests, "SocketInetLoopbackTest.LoopbackAddressRangeConnect");

  // Netstack3 does not support SO_REUSEADDR and only partially supports
  // SO_REUSEPORT for UDP sockets.
  SkipTest(tests, "All/SocketInetReusePortTest.UdpPortReuseMultiThread/*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackTest.NoReusePortFollowingReusePort/TCP");

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

  // Netstack3 does not support TCP (yet).

  // Skip tests that will otherwise hang forever.
  // TODO(b/245940107): un-skip some of these when the data path is ready.
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPResetAfterClose/*");
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPAcceptBacklogSizes/*");
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPListenClose/*");
  SkipTest(tests, "All/SocketInetLoopbackTest.TCPListenUnbound/*");

  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCP/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPBacklog/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPInfoState/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenCloseConnectingRead/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenCloseDuringConnect/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenShutdown/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenShutdownConnectingRead/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenShutdownDuringConnect/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenShutdownListen/*");
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPNonBlockingConnectClose/*");
  ExpectFailure(tests, "All/SocketInetReusePortTest.TcpPortReuseMultiThread/*");

  // Netstack3 does not have complete support for multicast sockets.
  ExpectFailure(tests, "BadSocketPairArgs.ValidateErrForBadCallsToSocketPair");
  ExpectFailure(tests, "SocketInetLoopbackTest.LoopbackAddressRangeConnect");
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
