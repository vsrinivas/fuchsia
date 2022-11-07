// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <string>

#include "third_party/gvisor_syscall_tests/expects/expectations.h"

constexpr char kFastUdpEnvVar[] = "FAST_UDP";

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  if (std::getenv(kFastUdpEnvVar)) {
    // Fast UDP doesn't enforce recieve buffer limits due to the use of a zircon
    // socket.
    SkipTest(tests, "AllInetTests/UdpSocketTest.RecvBufLimits/*");
  } else {
    // TODO(https://fxbug.dev/104104): Remove sync expectations after Fast UDP
    // rollout.

    // https://fxbug.dev/45245
    ExpectFailure(tests, "AllUDPSockets/NonStreamSocketPairTest.SendMsgTooLarge/*");
  }

  // Tests that flake in Fuchsia's CQ.
  // https://fxbug.dev/114419
  SkipTest(tests, "AllInetTests/UdpSocketTest.SendToAddressOtherThanConnected/*");

  // https://fxbug.dev/45262
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.BasicSendmmsg/*");
  // https://fxbug.dev/45261
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.RecvmsgMsghdrFlagsCleared/*");
  // https://fxbug.dev/45261
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.RecvmsgPeekMsghdrFlagsCleared/*");
  // https://fxbug.dev/42041
  // Deadlock? Test makes no progress even when run in isolation.
  SkipTest(tests, "AllInetTests/UdpSocketTest.ReadShutdown/*");
  // https://fxbug.dev/42041
  // Deadlock? Test makes no progress even when run in isolation.
  SkipTest(tests, "AllInetTests/UdpSocketTest.ReadShutdownDifferentThread/*");
  // https://fxbug.dev/42040
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.FIONREADShutdown/*");
  // https://fxbug.dev/42040
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.FIONREADWriteShutdown/*");
  // https://fxbug.dev/42040
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.Fionread/*");
  // https://fxbug.dev/42040
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.FIONREADZeroLengthPacket/*");
  // https://fxbug.dev/42040
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.FIONREADZeroLengthWriteShutdown/*");
  // https://fxbug.dev/42043
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.SoTimestamp/*");
  // https://fxbug.dev/42043
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.TimestampIoctl/*");
  // https://fxbug.dev/42043
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.TimestampIoctlNothingRead/*");
  // https://fxbug.dev/42043
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.TimestampIoctlPersistence/*");

  // https://fxbug.dev/45262
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.SendmmsgIsLimitedByMAXIOV/*");
  // https://fxbug.dev/45260
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.BasicRecvmmsg/*");
  // https://fxbug.dev/45260
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.RecvmmsgTimeoutBeforeRecv/*");
  // https://fxbug.dev/45260
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.RecvmmsgInvalidTimeout/*");
  // https://fxbug.dev/74837
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.SetAndGetBooleanSocketOptions/*");
  // https://fxbug.dev/67016
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.ReceiveOrigDstAddrDefault/*");
  ExpectFailure(tests, "AllUDPSockets/UDPSocketPairTest.SetAndGetReceiveOrigDstAddr/*");

  // https://fxbug.dev/84687
  ExpectFailure(tests, "AllInetTests/UdpSocketTest.DisconnectAfterBindToUnspecAndConnect/*");
  // https://fxbug.dev/52565
  // Fuchsia only supports IPV6_PKTINFO, and these variants exercise IP_PKTINFO.
  ExpectFailure(tests, "AllInetTests/UdpSocketControlMessagesTest.SetAndReceivePktInfo/0");
  ExpectFailure(tests, "AllInetTests/UdpSocketControlMessagesTest.SetAndReceivePktInfo/2");
  // https://fxbug.dev/74639
  ExpectFailure(tests, "AllUDPSockets/AllSocketPairTest.GetSetSocketRcvlowatOption/*");
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
