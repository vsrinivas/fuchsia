// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/perftest.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

namespace {

bool SyscallNullTest() {
    ZX_ASSERT(zx_syscall_test_0() == 0);
    return true;
}

bool SyscallManyArgsTest() {
    ZX_ASSERT(zx_syscall_test_8(1, 2, 3, 4, 5, 6, 7, 8) == 36);
    return true;
}

void RegisterTests() {
    perftest::RegisterSimpleTest<SyscallNullTest>("Syscall/Null");
    perftest::RegisterSimpleTest<SyscallManyArgsTest>("Syscall/ManyArgs");
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
