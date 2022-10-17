// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/gvisor_syscall_tests/expects/common.h"
#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  FilterTestsForLoopbackTarget(tests);

  // https://fxbug.dev/35593
  ExpectFailure(tests, "BadSocketPairArgs.ValidateErrForBadCallsToSocketPair");
  // https://fxbug.dev/61714
  ExpectFailure(tests, "All/SocketInetLoopbackTest.TCPListenShutdownListen/*");
  // https://fxbug.dev/35596
  // Deadlock? Test makes no progress even when run in isolation.
  SkipTest(tests, "All/SocketInetReusePortTest.TcpPortReuseMultiThread/*");
  // https://fxbug.dev/35596
  // Deadlock? Test makes no progress even when run in isolation.
  SkipTest(tests, "All/SocketInetReusePortTest.UdpPortReuseMultiThreadShort/*");
  // https://fxbug.dev/35596
  // Deadlock? Test makes no progress even when run in isolation.
  SkipTest(tests, "All/SocketInetReusePortTest.UdpPortReuseMultiThread/*");
  // https://fxbug.dev/44151
  for (const auto& parameter :
       {"V4AnyBindConnectSendTo", "V4AnyBindSendToConnect", "V4AnyConnectBindSendTo",
        "V4AnyConnectSendToBind", "V4AnySendToBindConnect", "V4AnySendToConnectBind",
        "V4LoopbackBindConnectSendTo", "V4LoopbackBindSendToConnect"}) {
    ExpectFailure(tests, TestSelector::ParameterizedTest("All", "DualStackSocketTest",
                                                         "AddressOperations", parameter));
  }
}

}  // namespace netstack_syscall_test
