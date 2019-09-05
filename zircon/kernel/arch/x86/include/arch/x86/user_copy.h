// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_USER_COPY_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_USER_COPY_H_

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// Typically we would not use structs as function return values, but in this case it enables us to
// very efficiently use the 2 registers for return values to encode the optional flags and va
// page fault values.
struct X64CopyToFromUserRet {
  zx_status_t status;
  uint pf_flags;
  vaddr_t pf_va;
};
static_assert(sizeof(X64CopyToFromUserRet) == 16, "X64CopyToFromUserRet has unexpected size");

#define X86_USER_COPY_CAPTURE_FAULTS (~(1ull << X86_PFR_RUN_FAULT_HANDLER_BIT))
#define X86_USER_COPY_DO_FAULTS (~0ull)

__BEGIN_CDECLS

// This function is used by arch_copy_from_user() and arch_copy_to_user().
// It should not be called anywhere except in the x86 usercopy
// implementation. If X86_USER_COPY_CAPTURE_FAULTS is passed as fault_return_mask then the returned
// struct will have pf_flags and pf_va filled out on pagefault, otherwise they should be ignored.
X64CopyToFromUserRet _x86_copy_to_or_from_user(void *dst, const void *src, size_t len,
                                               uint64_t *fault_return, uint64_t fault_return_mask);

__END_CDECLS

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_USER_COPY_H_
