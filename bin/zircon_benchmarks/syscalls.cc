// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"

#include "test_runner.h"

namespace {

void SyscallNullTest() {
  FXL_CHECK(zx_syscall_test_0() == 0);
}

void SyscallManyArgsTest() {
  FXL_CHECK(zx_syscall_test_8(1, 2, 3, 4, 5, 6, 7, 8) == 36);
}

__attribute__((constructor))
void RegisterTests() {
  fbenchmark::RegisterTestFunc<SyscallNullTest>("Syscall/Null");
  fbenchmark::RegisterTestFunc<SyscallManyArgsTest>("Syscall/ManyArgs");
}

}  // namespace
