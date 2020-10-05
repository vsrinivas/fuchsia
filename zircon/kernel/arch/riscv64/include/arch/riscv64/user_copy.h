// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_USER_COPY_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_USER_COPY_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <arch/arch_thread.h>

// Typically we would not use structs as function return values, but in this case it enables us to
// very efficiently use the 2 registers for return values to encode the optional flags and va
// page fault values.
struct Riscv64UserCopyRet {
  zx_status_t status;
  uint pf_flags;
  vaddr_t pf_va;
};
static_assert(sizeof(Riscv64UserCopyRet) == 16, "Riscv64UserCopyRet has unexpected size");

__BEGIN_CDECLS

// This is the same as memcpy, except that it takes the additional argument of
// &current_thread()->arch.data_fault_resume, where it temporarily stores the fault recovery PC for
// bad page faults to user addresses during the call.
Riscv64UserCopyRet _riscv64_user_copy(void* dst, const void* src, size_t len, uint64_t* fault_return);

__END_CDECLS

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_USER_COPY_H_
