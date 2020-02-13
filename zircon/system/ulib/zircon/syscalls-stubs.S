// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syscalls/zx-syscall-numbers.h>

// One of these macros is invoked by syscalls.inc for each syscall.

// These don't have kernel entry points.
#define VDSO_SYSCALL(...)

// These are the direct kernel entry points.
#define KERNEL_SYSCALL(name, type, attrs, nargs, arglist, prototype) \
  m_syscall zx_##name, ZX_SYS_##name, nargs, 1

// These are internal kernel entry points called by other vDSO functions.
#define INTERNAL_SYSCALL(name, type, attrs, nargs, arglist, prototype) \
  m_syscall zx_##name, ZX_SYS_##name, nargs, 0
#define BLOCKING_SYSCALL(...) INTERNAL_SYSCALL(__VA_ARGS__)

#include <lib/syscalls/syscalls.inc>

#undef VDSO_SYSCALL
#undef KERNEL_SYSCALL
#undef INTERNAL_SYSCALL
#undef BLOCKING_SYSCALL

// Compatibility aliases

#define ALIAS(oldname, newname) \
.globl oldname ;\
.type oldname,STT_FUNC ;\
oldname = newname ;\

// Currently no aliases!
