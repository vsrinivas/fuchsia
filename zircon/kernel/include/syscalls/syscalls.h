// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sys/types.h>

struct syscall_result {
    // The assembler relies on the fact that the ABI will return this in
    // x0,x1 (arm) or rax,rdx (x86) so we use plain types here to ensure this.
    uint64_t status;
    // Non-zero if thread was signaled.
    uint64_t is_signaled;
};

struct syscall_result unknown_syscall(uint64_t syscall_num, uint64_t ip);
