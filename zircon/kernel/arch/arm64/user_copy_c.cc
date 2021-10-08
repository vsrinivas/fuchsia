// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/user_copy/internal.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <arch/arch_thread.h>
#include <arch/user_copy.h>
#include <kernel/thread.h>
#include <vm/vm.h>

#define ARM64_USER_COPY_CAPTURE_FAULTS (~(1ull << ARM64_DFR_RUN_FAULT_HANDLER_BIT))
#define ARM64_USER_COPY_DO_FAULTS (~0ull)
static constexpr size_t kUserAspaceTop = (USER_ASPACE_BASE + USER_ASPACE_SIZE);

// Typically we would not use structs as function return values, but in this case it enables us to
// very efficiently use the 2 registers for return values to encode the optional flags and va
// page fault values.
struct Arm64UserCopyRet {
  zx_status_t status;
  uint pf_flags;
  vaddr_t pf_va;
};
static_assert(sizeof(Arm64UserCopyRet) == 16, "Arm64UserCopyRet has unexpected size");

// This is the same as memcpy, except that it takes the additional argument of
// &current_thread()->arch.data_fault_resume, where it temporarily stores the fault recovery PC for
// bad page faults to user addresses during the call, and a fault_return_mask. If
// ARM64_USER_COPY_CAPTURE_FAULTS is passed as fault_return_mask then the returned struct will have
// pf_flags and pf_va filled out on pagefault, otherwise they should be ignored. arch_copy_from_user
// and arch_copy_to_user should be the only callers of this.
extern "C" Arm64UserCopyRet _arm64_user_copy(void* dst, const void* src, size_t len,
                                             uint64_t* fault_return, uint64_t fault_return_mask);

zx_status_t arch_copy_from_user(void* dst, const void* src, size_t len) {
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT(arch_num_spinlocks_held() == 0);

  // The assembly code just does memcpy with fault handling.  This is
  // the security check that an address from the user is actually a
  // valid userspace address so users can't access kernel memory.
  if (!is_user_address_range(reinterpret_cast<vaddr_t>(src), len)) {
    return ZX_ERR_INVALID_ARGS;
  }
  // Spectre V1: Confine {src, len} to user addresses to prevent the kernel from speculatively
  // reading user-controlled addresses.
  internal::confine_user_address_range(reinterpret_cast<vaddr_t*>(&src), &len, kUserAspaceTop);

  return _arm64_user_copy(dst, src, len, &Thread::Current::Get()->arch().data_fault_resume,
                          ARM64_USER_COPY_DO_FAULTS)
      .status;
}

zx_status_t arch_copy_to_user(void* dst, const void* src, size_t len) {
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT(arch_num_spinlocks_held() == 0);

  if (!is_user_address_range(reinterpret_cast<vaddr_t>(dst), len)) {
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
  if (!is_user_address_range(reinterpret_cast<vaddr_t>(src), len)) {
    return UserCopyCaptureFaultsResult{ZX_ERR_INVALID_ARGS};
  }
  // Spectre V1: Confine {src, len} to user addresses to prevent the kernel from speculatively
  // reading user-controlled addresses.
  internal::confine_user_address_range(reinterpret_cast<vaddr_t*>(&src), &len, kUserAspaceTop);

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
  if (!is_user_address_range(reinterpret_cast<vaddr_t>(dst), len)) {
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
