// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/user_copy/internal.h>

#include <arch/user_copy.h>
#include <arch/riscv64/user_copy.h>
#include <kernel/thread.h>
#include <vm/vm.h>

extern "C" Riscv64UserCopyRet _riscv64_user_copy(void* dst, const void* src, size_t len, uint64_t* fault_return);

zx_status_t arch_copy_from_user(void* dst, const void* src, size_t len) {
  // The assembly code just does memcpy with fault handling.  This is
  // the security check that an address from the user is actually a
  // valid userspace address so users can't access kernel memory.
  if (!is_user_address_range(reinterpret_cast<vaddr_t>(src), len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  return _riscv64_user_copy(dst, src, len, &Thread::Current::Get()->arch().data_fault_resume)
      .status;
}

zx_status_t arch_copy_to_user(void* dst, const void* src, size_t len) {
  if (!is_user_address_range(reinterpret_cast<vaddr_t>(dst), len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  return _riscv64_user_copy(dst, src, len, &Thread::Current::Get()->arch().data_fault_resume)
      .status;
}

UserCopyCaptureFaultsResult arch_copy_from_user_capture_faults(void* dst, const void* src,
                                                               size_t len) {
  // The assembly code just does memcpy with fault handling.  This is
  // the security check that an address from the user is actually a
  // valid userspace address so users can't access kernel memory.
  if (!is_user_address_range(reinterpret_cast<vaddr_t>(src), len)) {
    return UserCopyCaptureFaultsResult{ZX_ERR_INVALID_ARGS};
  }

  Riscv64UserCopyRet ret =
      _riscv64_user_copy(dst, src, len, &Thread::Current::Get()->arch().data_fault_resume);

  // If a fault didn't occur, and ret.status == ZX_OK, this will copy garbage data. It is the
  // responsibility of the caller to check the status and ignore.
  if (ret.status == ZX_OK) {
    return UserCopyCaptureFaultsResult{ZX_OK};
  } else {
    return {ret.status, {ret.pf_va, ret.pf_flags}};
  }
  return UserCopyCaptureFaultsResult{ZX_OK};
}

UserCopyCaptureFaultsResult arch_copy_to_user_capture_faults(void* dst, const void* src,
                                                             size_t len) {
  if (!is_user_address_range(reinterpret_cast<vaddr_t>(dst), len)) {
    return UserCopyCaptureFaultsResult{ZX_ERR_INVALID_ARGS};
  }

  Riscv64UserCopyRet ret =
      _riscv64_user_copy(dst, src, len, &Thread::Current::Get()->arch().data_fault_resume);

  // If a fault didn't occur, and ret.status == ZX_OK, this will copy garbage data. It is the
  // responsibility of the caller to check the status and ignore.
  if (ret.status == ZX_OK) {
    return UserCopyCaptureFaultsResult{ZX_OK};
  } else {
    return {ret.status, {ret.pf_va, ret.pf_flags}};
  }
}
