// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// mx_status_t bad_syscall(uint64_t num)
.global bad_syscall
.type bad_syscall, STT_FUNC
bad_syscall:

#if defined(__aarch64__)
    mov x16, x0
    svc #0
    ret
#elif defined(__x86_64__)
    mov %rdi, %rax
    syscall
    ret
#else
#error "Unsupported arch"
#endif
