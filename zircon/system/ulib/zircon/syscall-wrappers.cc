// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"

// One of these macros is invoked by syscalls.inc for each syscall.

// These don't need wrappers.
#define VDSO_SYSCALL(...)
#define KERNEL_SYSCALL(...)
#define INTERNAL_SYSCALL(...)

#define BLOCKING_SYSCALL(name, type, attrs, nargs, arglist, prototype) \
  __EXPORT attrs type _zx_##name prototype {                           \
    type ret;                                                          \
    do {                                                               \
      ret = SYSCALL_zx_##name arglist;                                 \
    } while (unlikely(ret == ZX_ERR_INTERNAL_INTR_RETRY));             \
    return ret;                                                        \
  }                                                                    \
  VDSO_INTERFACE_FUNCTION(zx_##name);

#ifdef __clang__
#define _ZX_SYSCALL_ANNO(anno) __attribute__((anno))
#else
#define _ZX_SYSCALL_ANNO(anno)
#endif

#include <lib/syscalls/syscalls.inc>

#undef VDSO_SYSCALL
#undef KERNEL_SYSCALL
#undef INTERNAL_SYSCALL
#undef BLOCKING_SYSCALL
