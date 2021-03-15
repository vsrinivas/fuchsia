// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <assert.h>
#include <string.h>
#include <trace.h>
#include <zircon/types.h>

#include <arch/user_copy.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <kernel/thread.h>
#include <vm/vm.h>

#include "arch/x86/user_copy.h"

#define LOCAL_TRACE 0

enum class CopyDirection { ToUser, FromUser };

static inline bool ac_flag(void) { return x86_save_flags() & X86_FLAGS_AC; }

static bool can_access(const void* base, size_t len) {
  LTRACEF("can_access: base %p, len %zu\n", base, len);

  // We don't care about whether pages are actually mapped or what their
  // permissions are, as long as they are in the user address space.  We
  // rely on a page fault occurring if an actual permissions error occurs.
  return is_user_address_range(reinterpret_cast<vaddr_t>(base), len);
}

template <uint64_t FAULT_RETURN_MASK, CopyDirection DIRECTION>
static UserCopyCaptureFaultsResult _arch_copy_to_from_user(void* dst, const void* src, size_t len) {
  // There are exactly two version of this function which may be expanded.
  // Anything else would be an error which should be caught at compile time.
  static_assert((FAULT_RETURN_MASK == X86_USER_COPY_DO_FAULTS) ||
                    (FAULT_RETURN_MASK == X86_USER_COPY_CAPTURE_FAULTS),
                "arch_copy_(to|from)_user routines must either capture faults, or simply take the "
                "fault but return no details.");

  // If we have the SMAP feature, then AC should only be set when running
  // _x86_copy_to_or_from_user. If we don't have the SMAP feature, then we don't care if AC is set
  // or not.
  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());

  // Check to make sure that our user space address exists entirely within the
  // possible user space address range.  If not, then we are not going to make
  // _any_ attempt to copy the data at all.  If this direction of this copy is
  // ToUser, then our "user" address to test is the destination address.
  // Otherwise, it is the source address.
  //
  // In either case, if we are not going to even try, then there is no fault
  // address or flags to propagate, just a failed status code.
  if (!can_access((DIRECTION == CopyDirection::ToUser) ? dst : src, len)) {
    return UserCopyCaptureFaultsResult{ZX_ERR_INVALID_ARGS};
  }

  // Spectre V1 - force resolution of can_access() before attempting to copy
  // from user memory.  A poisoned conditional branch predictor can be used to
  // force the kernel to read any kernel address (speculatively); dependent
  // operations can leak the values read-in.
  //
  // Note, this is only needed if we are copying data to the user address space.
  // We skip this fence in the case that we are copying from the user address
  // space into the kernel space.
  if constexpr (DIRECTION == CopyDirection::ToUser) {
    __asm__ __volatile__("lfence" ::: "memory");
  }

  Thread* thr = Thread::Current::Get();
  X64CopyToFromUserRet ret =
      _x86_copy_to_or_from_user(dst, src, len, &thr->arch().page_fault_resume, FAULT_RETURN_MASK);
  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());

  // In the DO_FAULTS version of this expansion, do not make any attempt to
  // propagate the fault address and flags.  We only propagate fault info in the
  // CAPTURE_FAULTS version, and then only if we actually take a fault, not if
  // we succeed.
  if constexpr (FAULT_RETURN_MASK == X86_USER_COPY_DO_FAULTS) {
    return UserCopyCaptureFaultsResult{ret.status};
  } else {
    if (ret.status == ZX_OK) {
      return UserCopyCaptureFaultsResult{ZX_OK};
    } else {
      return {ret.status, {ret.pf_va, ret.pf_flags}};
    }
  }
}

zx_status_t arch_copy_from_user(void* dst, const void* src, size_t len) {
  // It should always be safe to invoke "error_value" here.  The DO_FAULTs
  // version of the copy routine will never return fault information.  In a
  // release build, all of this should vanish and the status should just end up
  // getting returned directly.
  return _arch_copy_to_from_user<X86_USER_COPY_DO_FAULTS, CopyDirection::FromUser>(dst, src, len)
      .status;
}

UserCopyCaptureFaultsResult arch_copy_from_user_capture_faults(void* dst, const void* src,
                                                               size_t len) {
  return _arch_copy_to_from_user<X86_USER_COPY_CAPTURE_FAULTS, CopyDirection::FromUser>(dst, src,
                                                                                        len);
}

zx_status_t arch_copy_to_user(void* dst, const void* src, size_t len) {
  return _arch_copy_to_from_user<X86_USER_COPY_DO_FAULTS, CopyDirection::ToUser>(dst, src, len)
      .status;
}

UserCopyCaptureFaultsResult arch_copy_to_user_capture_faults(void* dst, const void* src,
                                                             size_t len) {
  return _arch_copy_to_from_user<X86_USER_COPY_CAPTURE_FAULTS, CopyDirection::ToUser>(dst, src,
                                                                                      len);
}
