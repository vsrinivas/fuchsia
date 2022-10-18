// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_GVISOR_SYSCALL_TESTS_EXPECTS_COMMON_H_
#define THIRD_PARTY_GVISOR_SYSCALL_TESTS_EXPECTS_COMMON_H_

#include "expectations.h"

namespace netstack_syscall_test {

// The `loopback` target runs only a subset of the tests in its included
// source files. This method skips all tests besides that subset.
void FilterTestsForLoopbackTarget(TestMap& tests);

// The `loopback_isolated_tcp_fin_wait` target runs only a subset of the tests in its
// included source files. This method skips all tests besides that subset.
void FilterTestsForLoopbackIsolatedTcpFinWaitTarget(TestMap& tests);

// The `loopback_isolated_tcp_linger_timeout` target runs only a subset of the tests in its
// included source files. This method skips all tests besides that subset.
void FilterTestsForLoopbackIsolatedTcpLingerTimeoutTarget(TestMap& tests);

// The `loopback_isolated` target runs only a subset of the tests in its
// included source files. This method skips all tests besides that subset.
void FilterTestsForLoopbackIsolatedTarget(TestMap& tests);

// The `loopback_tcp_backlog` target runs only a subset of the tests in its included
// source files. This method skips all tests besides that subset.
void FilterTestsForLoopbackTcpBacklogTarget(TestMap& tests);

// The `loopback_tcp_accept` target runs only a subset of the tests in its included
// source files. This method skips all tests besides that subset.
void FilterTestsForLoopbackTcpAcceptTarget(TestMap& tests);

// The `loopback_tcp_accept_backlog` target runs only a subset of the tests in its included
// source files. This method skips all tests besides that subset.
void FilterTestsForLoopbackTcpAcceptBacklogTarget(TestMap& tests);

}  // namespace netstack_syscall_test

#endif  // THIRD_PARTY_GVISOR_SYSCALL_TESTS_EXPECTS_COMMON_H_
