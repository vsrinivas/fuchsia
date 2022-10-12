// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/gvisor_syscall_tests/expects/common.h"
#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  AddCommonExpectsTcpNetstack2(tests);
  // https://fxbug.dev/46211
  // Deadlock? Test makes no progress even when run in isolation.
  SkipTest(tests, "BlockingTCPSockets/BlockingStreamSocketPairTest.SendMsgTooLarge/*");
  // https://fxbug.dev/42692
  ExpectFailure(tests,
                "BlockingTCPSockets/"
                "BlockingStreamSocketPairTest.RecvLessThanBufferWaitAll/*");
}

}  // namespace netstack_syscall_test
