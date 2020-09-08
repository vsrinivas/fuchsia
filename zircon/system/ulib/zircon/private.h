// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZIRCON_PRIVATE_H_
#define ZIRCON_SYSTEM_ULIB_ZIRCON_PRIVATE_H_

#include <zircon/compiler.h>
#include <zircon/syscalls.h>

// This defines the struct shared with the kernel.
#include <lib/userabi/vdso-constants.h>

extern __LOCAL const struct vdso_constants DATA_CONSTANTS;

extern "C" {

// This declares the VDSO_zx_* aliases for the vDSO entry points.
// Calls made from within the vDSO must use these names rather than
// the public names so as to avoid PLT entries.

// One of these macros is invoked by syscalls.inc for each syscall.

// These don't have kernel entry points.
#define VDSO_SYSCALL(name, type, attrs, nargs, arglist, prototype) \
  __LOCAL attrs extern type VDSO_zx_##name prototype;

// These are the direct kernel entry points.
#define KERNEL_SYSCALL(name, type, attrs, nargs, arglist, prototype) \
  __LOCAL attrs extern type VDSO_zx_##name prototype;                \
  __LOCAL attrs extern type SYSCALL_zx_##name prototype;
#define INTERNAL_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)
#define BLOCKING_SYSCALL(...) INTERNAL_SYSCALL(__VA_ARGS__)

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
#undef _ZX_SYSCALL_ANNO

__LOCAL decltype(zx_clock_get_monotonic) CODE_clock_get_monotonic_via_kernel_ticks;
__LOCAL decltype(zx_deadline_after) CODE_deadline_after_via_kernel_mono;
__LOCAL decltype(zx_deadline_after) CODE_deadline_after_via_kernel_ticks;

#if __aarch64__
__LOCAL decltype(zx_ticks_get) CODE_ticks_get_arm_a73;
#endif

}  // extern "C"

// Code should define '_zx_foo' and then do 'VDSO_INTERFACE_FUNCTION(zx_foo);'
// to define the public name 'zx_foo' and the vDSO-private name 'VDSO_zx_foo'.
#define VDSO_INTERFACE_FUNCTION(name)                   \
  __EXPORT decltype(name) name __WEAK_ALIAS("_" #name); \
  decltype(name) VDSO_##name __LOCAL __ALIAS("_" #name)

// This symbol is expected to appear in the build-time vDSO symbol table so
// kernel/lib/userabi/ code can use it.
#define VDSO_KERNEL_EXPORT __attribute__((used))

#endif  // ZIRCON_SYSTEM_ULIB_ZIRCON_PRIVATE_H_
