// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This is linked into some minimal test phys executables that are run
// directly as QEMU kernels.  It provides the section name that phys.ld
// places first, with a trivial trampoline to call _start.  In real phys
// executables, this section holds either a target-specific header, or ZBI
// container and kernel item headers, pointing to the load image with
// _start as its entry point.

.section .boot.header, "ax", %progbits

#if defined(__aarch64__)
  b _start
#elif defined(__x86_64__) || defined(__i386__)
  jmp _start
#elif defined(__riscv)
  j _start
#else
#error "what machine??"
#endif
