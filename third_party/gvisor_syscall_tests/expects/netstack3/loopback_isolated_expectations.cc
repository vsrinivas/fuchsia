// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expects/common.h"
#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  FilterTestsForLoopbackIsolatedTarget(tests);

  // Netstack3 does not support TCP (yet).

  // Skip tests that will otherwise hang forever.
  // TODO(b/245940107): un-skip some of these when the data path is ready.
  ExpectFailure(tests, "All/SocketInetLoopbackIsolatedTest.TCPActiveCloseTimeWaitReuseTest/*");
  SkipTest(tests, "All/SocketInetLoopbackIsolatedTest.TCPActiveCloseTimeWaitTest/*");
  SkipTest(tests, "All/SocketInetLoopbackIsolatedTest.TCPPassiveCloseNoTimeWaitTest/*");
  ExpectFailure(tests,
                "All/SocketInetLoopbackIsolatedTest.TCPPassiveCloseNoTimeWaitReuseTest/"
                "*");
  ExpectFailure(tests,
                "AllFamilies/"
                "SocketMultiProtocolInetLoopbackIsolatedTest.BindToDeviceReusePort/TCP");

  // Netstack3 does not have complete support for multicast sockets.
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
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
