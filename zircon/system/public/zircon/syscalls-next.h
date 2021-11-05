// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_NEXT_H_
#define SYSROOT_ZIRCON_SYSCALLS_NEXT_H_

#ifndef _KERNEL

#include <zircon/syscalls.h>

__BEGIN_CDECLS

// Make sure this matches <zircon/syscalls.h>.
#define _ZX_SYSCALL_DECL(name, type, attrs, nargs, arglist, prototype) \
  extern attrs type zx_##name prototype;                               \
  extern attrs type _zx_##name prototype;

#ifdef __clang__
#define _ZX_SYSCALL_ANNO(attr) __attribute__((attr))
#else
#define _ZX_SYSCALL_ANNO(attr)  // Nothing for compilers without the support.
#endif

#include <zircon/syscalls/internal/cdecls-next.inc>

#undef _ZX_SYSCALL_ANNO
#undef _ZX_SYSCALL_DECL

__END_CDECLS

#endif  // !_KERNEL

// ====== Pager writeback support ====== //
//
// Make sure the constants defined here do not overlap with VMO / pager constants defined in
// <zircon/types.h> or <zircon/syscalls/port.h>. These constants will eventually get moved over.

// VM Object creation options
#define ZX_VMO_TRAP_DIRTY ((uint32_t)1u << 3)

// zx_packet_page_request_t::command
#define ZX_PAGER_VMO_DIRTY ((uint16_t)2)

// ====== End of pager writeback support ====== //

#endif  // SYSROOT_ZIRCON_SYSCALLS_NEXT_H_
