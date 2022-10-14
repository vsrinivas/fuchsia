// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_GVISOR_SYSCALL_TESTS_EXPECTS_COMMON_H_
#define THIRD_PARTY_GVISOR_SYSCALL_TESTS_EXPECTS_COMMON_H_

#include "expectations.h"

namespace netstack_syscall_test {

// The `tcp` and `tcp_blocking` tests have shared expectations when run
// against Netstack2 stacks. This method adds those expectations to the test map.
void AddCommonExpectsTcpNetstack2(TestMap& tests);

// The `tcp_backlog` target runs only a subset of the tests in its included
// source files. This method skips all tests besides that subset.
void AddSkippedTestsTcpBacklog(TestMap& tests);

// The `tcp_accept_backlog` target runs only a subset of the tests in its included
// source files. This method skips all tests besides that subset.
void AddSkippedTestsTcpAcceptBacklog(TestMap& tests);

// The `loopback` target runs only a subset of the tests in its included
// source files. This method skips all tests besides that subset.
void AddSkippedTestsLoopback(TestMap& tests);

// The `loopback_isolated` target runs only a subset of the tests in its
// inclueded source files. This method skips all tests besides that subset.
void AddSkippedTestsLoopbackIsolated(TestMap& tests);

// The `fin_wait` target runs only a subset of the tests in its
// inclueded source files. This method skips all tests besides that subset.
void AddSkippedTestsFinWait(TestMap& tests);

// The `linger_timeout` target runs only a subset of the tests in its
// inclueded source files. This method skips all tests besides that subset.
void AddSkippedTestsLingerTimeout(TestMap& tests);

}  // namespace netstack_syscall_test

#endif  // THIRD_PARTY_GVISOR_SYSCALL_TESTS_EXPECTS_COMMON_H_
