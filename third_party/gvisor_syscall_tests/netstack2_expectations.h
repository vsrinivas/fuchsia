// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_GVISOR_SYSCALL_TESTS_NETSTACK2_EXPECTATIONS_H_
#define THIRD_PARTY_GVISOR_SYSCALL_TESTS_NETSTACK2_EXPECTATIONS_H_

#include "expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTestsCommonNetstack2(TestMap& tests);

}  // namespace netstack_syscall_test

#endif  // THIRD_PARTY_GVISOR_SYSCALL_TESTS_NETSTACK2_EXPECTATIONS_H_
