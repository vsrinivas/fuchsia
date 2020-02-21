// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "arch/x86/user_copy.h"

#include <assert.h>
#include <lib/code_patching.h>
#include <string.h>
#include <trace.h>
#include <zircon/types.h>

#include <arch/user_copy.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <kernel/thread.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

CODE_TEMPLATE(kStacInstruction, "stac")
CODE_TEMPLATE(kClacInstruction, "clac")
static const uint8_t kNopInstruction = 0x90;

extern "C" {

void fill_out_stac_instruction(const CodePatchInfo* patch) {
  const size_t kSize = 3;
  DEBUG_ASSERT(patch->dest_size == kSize);
  DEBUG_ASSERT(kStacInstructionEnd - kStacInstruction == kSize);
  if (g_x86_feature_has_smap) {
    memcpy(patch->dest_addr, kStacInstruction, kSize);
  } else {
    memset(patch->dest_addr, kNopInstruction, kSize);
  }
}

void fill_out_clac_instruction(const CodePatchInfo* patch) {
  const size_t kSize = 3;
  DEBUG_ASSERT(patch->dest_size == kSize);
  DEBUG_ASSERT(kClacInstructionEnd - kClacInstruction == kSize);
  if (g_x86_feature_has_smap) {
    memcpy(patch->dest_addr, kClacInstruction, kSize);
  } else {
    memset(patch->dest_addr, kNopInstruction, kSize);
  }
}

void x86_usercopy_select(const CodePatchInfo* patch) {
  // The user copy patch area is 16-byte aligned; make sure the block is 16-bytes in size too, to
  // ensure that the jump that exits it jumps to a 16-byte aligned address, per recommendations in
  // the AMD Family 17h and the Intel Optimization guides.
  const size_t kSize = 16;
  DEBUG_ASSERT(patch->dest_size == kSize);

  memset(patch->dest_addr, kNopInstruction, kSize);
  if (x86_feature_test(X86_FEATURE_ERMS) ||
      (x86_get_microarch_config()->x86_microarch == X86_MICROARCH_AMD_ZEN)) {
    patch->dest_addr[0] = 0xf3;  // rep movsb %ds:(%rsi),%es:(%rdi)
    patch->dest_addr[1] = 0xa4;
  } else {
    patch->dest_addr[0] = 0x48;  // shrq $3, %rcx
    patch->dest_addr[1] = 0xc1;
    patch->dest_addr[2] = 0xe9;
    patch->dest_addr[3] = 0x03;
    patch->dest_addr[4] = 0xf3;  // rep movsq %ds:(%rsi),%es:(%rdi)
    patch->dest_addr[5] = 0x48;
    patch->dest_addr[6] = 0xa5;
    patch->dest_addr[7] = 0x83;  // andl $7, %edx
    patch->dest_addr[8] = 0xe2;
    patch->dest_addr[9] = 0x07;
    patch->dest_addr[10] = 0x74;  // je +04  -- jumps to aligned 16by after this fragment
    patch->dest_addr[11] = 0x04;
    patch->dest_addr[12] = 0x89;  // movl %edx, %ecx
    patch->dest_addr[13] = 0xd1;
    patch->dest_addr[14] = 0xf3;  // rep movsb %ds:(%rsi),%es:(%rdi)
    patch->dest_addr[15] = 0xa4;
  }
}
}

static inline bool ac_flag(void) { return x86_save_flags() & X86_FLAGS_AC; }

static bool can_access(const void* base, size_t len) {
  LTRACEF("can_access: base %p, len %zu\n", base, len);

  // We don't care about whether pages are actually mapped or what their
  // permissions are, as long as they are in the user address space.  We
  // rely on a page fault occurring if an actual permissions error occurs.
  DEBUG_ASSERT(x86_get_cr0() & X86_CR0_WP);
  return is_user_address_range(reinterpret_cast<vaddr_t>(base), len);
}

static X64CopyToFromUserRet _arch_copy_from_user(void* dst, const void* src, size_t len,
                                                 uint64_t fault_return_mask) {
  // If we have the SMAP feature, then AC should only be set when running
  // _x86_copy_to_or_from_user. If we don't have the SMAP feature, then we don't care if AC is set
  // or not.
  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());

  if (!can_access(src, len))
    return (X64CopyToFromUserRet){.status = ZX_ERR_INVALID_ARGS, .pf_flags = 0, .pf_va = 0};

  // Spectre V1 - force resolution of can_access() before attempting to copy from user memory.
  // A poisoned conditional branch predictor can be used to force the kernel to read any kernel
  // address (speculatively); dependent operations can leak the values read-in.
  __asm__ __volatile__("lfence" ::: "memory");

  Thread* thr = Thread::Current::Get();
  X64CopyToFromUserRet ret =
      _x86_copy_to_or_from_user(dst, src, len, &thr->arch_.page_fault_resume, fault_return_mask);

  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());
  return ret;
}

zx_status_t arch_copy_from_user(void* dst, const void* src, size_t len) {
  return _arch_copy_from_user(dst, src, len, X86_USER_COPY_DO_FAULTS).status;
}

zx_status_t arch_copy_from_user_capture_faults(void* dst, const void* src, size_t len,
                                               vaddr_t* pf_va, uint* pf_flags) {
  X64CopyToFromUserRet ret = _arch_copy_from_user(dst, src, len, X86_USER_COPY_CAPTURE_FAULTS);
  // If a fault didn't occur, and ret.status == ZX_OK, this will copy garbage data. It is the
  // responsibility of the caller to check the status and ignore.
  *pf_va = ret.pf_va;
  *pf_flags = ret.pf_flags;
  return ret.status;
}

static X64CopyToFromUserRet _arch_copy_to_user(void* dst, const void* src, size_t len,
                                               uint64_t fault_return_mask) {
  // If we have the SMAP feature, then AC should only be set when running
  // _x86_copy_to_or_from_user. If we don't have the SMAP feature, then we don't care if AC is set
  // or not.
  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());

  if (!can_access(dst, len))
    return (X64CopyToFromUserRet){.status = ZX_ERR_INVALID_ARGS, .pf_flags = 0, .pf_va = 0};

  Thread* thr = Thread::Current::Get();
  X64CopyToFromUserRet ret =
      _x86_copy_to_or_from_user(dst, src, len, &thr->arch_.page_fault_resume, fault_return_mask);

  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());
  return ret;
}

zx_status_t arch_copy_to_user(void* dst, const void* src, size_t len) {
  return _arch_copy_to_user(dst, src, len, X86_USER_COPY_DO_FAULTS).status;
}

zx_status_t arch_copy_to_user_capture_faults(void* dst, const void* src, size_t len, vaddr_t* pf_va,
                                             uint* pf_flags) {
  X64CopyToFromUserRet ret = _arch_copy_to_user(dst, src, len, X86_USER_COPY_CAPTURE_FAULTS);
  // If a fault didn't occur, and ret.status == ZX_OK, this will copy garbage data. It is the
  // responsibility of the caller to check the status and ignore.
  *pf_va = ret.pf_va;
  *pf_flags = ret.pf_flags;
  return ret.status;
}
