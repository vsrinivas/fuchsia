// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "user-copy.h"

#include <lib/user_copy/internal.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <arch/arch_thread.h>
#include <arch/user_copy.h>
#include <kernel/thread.h>
#include <vm/vm.h>

#define ARM64_USER_COPY_CAPTURE_FAULTS (~(1ull << ARM64_DFR_RUN_FAULT_HANDLER_BIT))
#define ARM64_USER_COPY_DO_FAULTS (~0ull)

zx_status_t arch_copy_from_user(void* dst, const void* src, size_t len) {
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT(arch_num_spinlocks_held() == 0);
  // The copy might trigger a page fault that needs to block on a user pager request, in which case
  // it is not permitted to be holding locks. To ensure this doesn't accidentally work if a copy
  // does not happen to trigger a pager, we insist that all copies do not hold locks. This method is
  // an empty inline function if lockdep is not enabled.
  lockdep::AssertNoLocksHeld();

  // The assembly code just does memcpy with fault handling.  This is
  // the security check that an address from the user is actually a
  // valid userspace address so users can't access kernel memory.
  if (!is_user_accessible_range(reinterpret_cast<vaddr_t>(src), len)) {
    return ZX_ERR_INVALID_ARGS;
  }
  // Spectre V1: Confine {src, len} to user addresses to prevent the kernel from speculatively
  // reading user-controlled addresses.
  internal::validate_user_accessible_range(reinterpret_cast<vaddr_t*>(&src), &len);

  return _arm64_user_copy(dst, src, len, &Thread::Current::Get()->arch().data_fault_resume,
                          ARM64_USER_COPY_DO_FAULTS)
      .status;
}

zx_status_t arch_copy_to_user(void* dst, const void* src, size_t len) {
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT(arch_num_spinlocks_held() == 0);
  // See comment above.
  lockdep::AssertNoLocksHeld();

  if (!is_user_accessible_range(reinterpret_cast<vaddr_t>(dst), len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  return _arm64_user_copy(dst, src, len, &Thread::Current::Get()->arch().data_fault_resume,
                          ARM64_USER_COPY_DO_FAULTS)
      .status;
}

UserCopyCaptureFaultsResult arch_copy_from_user_capture_faults(void* dst, const void* src,
                                                               size_t len) {
  // The assembly code just does memcpy with fault handling.  This is
  // the security check that an address from the user is actually a
  // valid userspace address so users can't access kernel memory.
  if (!is_user_accessible_range(reinterpret_cast<vaddr_t>(src), len)) {
    return UserCopyCaptureFaultsResult{ZX_ERR_INVALID_ARGS};
  }
  // Spectre V1: Confine {src, len} to user addresses to prevent the kernel from speculatively
  // reading user-controlled addresses.
  internal::validate_user_accessible_range(reinterpret_cast<vaddr_t*>(&src), &len);

  Arm64UserCopyRet ret =
      _arm64_user_copy(dst, src, len, &Thread::Current::Get()->arch().data_fault_resume,
                       ARM64_USER_COPY_CAPTURE_FAULTS);

  // If a fault didn't occur, and ret.status == ZX_OK, this will copy garbage data. It is the
  // responsibility of the caller to check the status and ignore.
  if (ret.status == ZX_OK) {
    return UserCopyCaptureFaultsResult{ZX_OK};
  } else {
    return {ret.status, {ret.pf_va, ret.pf_flags}};
  }
}

UserCopyCaptureFaultsResult arch_copy_to_user_capture_faults(void* dst, const void* src,
                                                             size_t len) {
  if (!is_user_accessible_range(reinterpret_cast<vaddr_t>(dst), len)) {
    return UserCopyCaptureFaultsResult{ZX_ERR_INVALID_ARGS};
  }

  Arm64UserCopyRet ret =
      _arm64_user_copy(dst, src, len, &Thread::Current::Get()->arch().data_fault_resume,
                       ARM64_USER_COPY_CAPTURE_FAULTS);

  // If a fault didn't occur, and ret.status == ZX_OK, this will copy garbage data. It is the
  // responsibility of the caller to check the status and ignore.
  if (ret.status == ZX_OK) {
    return UserCopyCaptureFaultsResult{ZX_OK};
  } else {
    return {ret.status, {ret.pf_va, ret.pf_flags}};
  }
}
