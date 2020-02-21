// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/user_copy.h>
#include <kernel/thread.h>
#include <vm/vm.h>

#include "arch/arm64/user_copy.h"

zx_status_t arch_copy_from_user(void* dst, const void* src, size_t len) {
  // The assembly code just does memcpy with fault handling.  This is
  // the security check that an address from the user is actually a
  // valid userspace address so users can't access kernel memory.
  if (!is_user_address_range(reinterpret_cast<vaddr_t>(src), len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  return _arm64_user_copy(dst, src, len, &Thread::Current::Get()->arch_.data_fault_resume,
                          ARM64_USER_COPY_DO_FAULTS)
      .status;
}

zx_status_t arch_copy_to_user(void* dst, const void* src, size_t len) {
  if (!is_user_address_range(reinterpret_cast<vaddr_t>(dst), len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  return _arm64_user_copy(dst, src, len, &Thread::Current::Get()->arch_.data_fault_resume,
                          ARM64_USER_COPY_DO_FAULTS)
      .status;
}

zx_status_t arch_copy_from_user_capture_faults(void* dst, const void* src, size_t len,
                                               vaddr_t* pf_va, uint* pf_flags) {
  // The assembly code just does memcpy with fault handling.  This is
  // the security check that an address from the user is actually a
  // valid userspace address so users can't access kernel memory.
  if (!is_user_address_range(reinterpret_cast<vaddr_t>(src), len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Arm64UserCopyRet ret =
      _arm64_user_copy(dst, src, len, &Thread::Current::Get()->arch_.data_fault_resume,
                       ARM64_USER_COPY_CAPTURE_FAULTS);
  // If a fault didn't occur, and ret.status == ZX_OK, this will copy garbage data. It is the
  // responsibility of the caller to check the status and ignore.
  *pf_va = ret.pf_va;
  *pf_flags = ret.pf_flags;
  return ret.status;
}

zx_status_t arch_copy_to_user_capture_faults(void* dst, const void* src, size_t len, vaddr_t* pf_va,
                                             uint* pf_flags) {
  if (!is_user_address_range(reinterpret_cast<vaddr_t>(dst), len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Arm64UserCopyRet ret =
      _arm64_user_copy(dst, src, len, &Thread::Current::Get()->arch_.data_fault_resume,
                       ARM64_USER_COPY_CAPTURE_FAULTS);
  // If a fault didn't occur, and ret.status == ZX_OK, this will copy garbage data. It is the
  // responsibility of the caller to check the status and ignore.
  *pf_va = ret.pf_va;
  *pf_flags = ret.pf_flags;
  return ret.status;
}
