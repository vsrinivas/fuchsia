// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_SYSCALLS_SYSCALLS_H_
#define ZIRCON_KERNEL_INCLUDE_SYSCALLS_SYSCALLS_H_

#include <sys/types.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

struct syscall_result {
  // The assembler relies on the fact that the ABI will return this in
  // x0,x1 (arm) or rax,rdx (x86) so we use plain types here to ensure this.
  uint64_t status;
  // Non-zero if thread was signaled.
  uint64_t is_signaled;
};

struct syscall_result unknown_syscall(uint64_t syscall_num, uint64_t ip);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_SYSCALLS_SYSCALLS_H_
