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
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
