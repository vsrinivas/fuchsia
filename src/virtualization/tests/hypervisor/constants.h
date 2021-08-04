// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_HYPERVISOR_CONSTANTS_H_
#define SRC_VIRTUALIZATION_TESTS_HYPERVISOR_CONSTANTS_H_

#include <limits.h>

#if __x86_64__
#include "arch/x64/constants.h"
#elif __aarch64__
#include "arch/arm64/constants.h"
#else
#error Unknown architecture.
#endif

#define VMO_SIZE 0x1000000
#define TRAP_PORT 0x11
#define TRAP_ADDR (VMO_SIZE - PAGE_SIZE * 2)

#define EXIT_TEST_ADDR (VMO_SIZE - PAGE_SIZE)              // Trap address to indicate test success
#define EXIT_TEST_FAILURE_ADDR (VMO_SIZE - PAGE_SIZE + 8)  // Trap adderss to indicate test failure

#endif  // SRC_VIRTUALIZATION_TESTS_HYPERVISOR_CONSTANTS_H_
