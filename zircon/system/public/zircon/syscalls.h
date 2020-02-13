// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_H_
#define SYSROOT_ZIRCON_SYSCALLS_H_

#include <zircon/syscalls/object.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/profile.h>
#include <zircon/syscalls/types.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define _ZX_SYSCALL_DECL(name, type, attrs, nargs, arglist, prototype) \
  extern attrs type zx_##name prototype;                               \
  extern attrs type _zx_##name prototype;

#ifdef __clang__
#define _ZX_SYSCALL_ANNO(attr) __attribute__((attr))
#else
#define _ZX_SYSCALL_ANNO(attr)  // Nothing for compilers without the support.
#endif

#include <zircon/syscalls/internal/cdecls.inc>

#undef _ZX_SYSCALL_ANNO
#undef _ZX_SYSCALL_DECL

// Compatibility wrappers for deprecated syscalls also go here, when
// there are any.

__END_CDECLS

#endif  // SYSROOT_ZIRCON_SYSCALLS_H_
