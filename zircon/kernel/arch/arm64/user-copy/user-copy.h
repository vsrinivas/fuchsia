// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <zircon/errors.h>
#include <zircon/types.h>

#ifndef ZIRCON_KERNEL_ARCH_ARM64_USER_COPY_USER_COPY_H_
#define ZIRCON_KERNEL_ARCH_ARM64_USER_COPY_USER_COPY_H_

// Typically we would not use structs as function return values, but in this case it enables us to
// very efficiently use the 2 registers for return values to encode the optional flags and va
// page fault values.
struct Arm64UserCopyRet {
  zx_status_t status;
  unsigned int pf_flags;
  zx_vaddr_t pf_va;
};
static_assert(sizeof(Arm64UserCopyRet) == 16, "Arm64UserCopyRet has unexpected size");

extern "C" Arm64UserCopyRet _arm64_user_copy(void* dst, const void* src, size_t len,
                                             uint64_t* fault_return, uint64_t fault_return_mask);

#endif  // ZIRCON_KERNEL_ARCH_ARM64_USER_COPY_USER_COPY_H_
