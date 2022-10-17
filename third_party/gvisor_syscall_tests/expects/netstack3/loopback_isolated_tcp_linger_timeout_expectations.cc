// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "third_party/gvisor_syscall_tests/expects/common.h"
#include "third_party/gvisor_syscall_tests/expects/expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  AddSkippedTestsLoopbackIsolatedTcpLingerTimeout(tests);

  // Currently NS3 can't run the Linger2Timeout suite.
  SkipTest(tests, "All/SocketInetLoopbackIsolatedTest.TCPLinger2TimeoutAfterClose/*");
}  // NOLINT(readability/fn_size)

}  // namespace netstack_syscall_test
