// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <string.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

#include <arch/debugger.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <arch/x86/registers.h>
#include <kernel/lockdep.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <vm/vm.h>

// Note on locking: The below functions need to read and write the register state and make sure that
// nothing happens with respect to scheduling that thread while this is happening. As a result they
// use ThreadLock. In most cases this will not be necessary but there are relatively few
// guarantees so we lock the scheduler. Since these functions are used mostly for debugging, this
// shouldn't be too significant a performance penalty.

namespace {

#define COPY_REG(out, in, reg) (out)->reg = (in)->reg
#define COPY_COMMON_REGS(out, in) \
  do {                            \
    COPY_REG(out, in, rax);       \
    COPY_REG(out, in, rbx);       \
    COPY_REG(out, in, rcx);       \
    COPY_REG(out, in, rdx);       \
    COPY_REG(out, in, rsi);       \
    COPY_REG(out, in, rdi);       \
    COPY_REG(out, in, rbp);       \
    COPY_REG(out, in, r8);        \
    COPY_REG(out, in, r9);        \
    COPY_REG(out, in, r10);       \
    COPY_REG(out, in, r11);       \
    COPY_REG(out, in, r12);       \
    COPY_REG(out, in, r13);       \
    COPY_REG(out, in, r14);       \
    COPY_REG(out, in, r15);       \
  } while (0)

void x86_fill_in_gregs_from_syscall(zx_thread_state_general_regs_t* out,
                                    const x86_syscall_general_regs_t* in) {
  COPY_COMMON_REGS(out, in);
  out->rip = in->rip;
  out->rsp = in->rsp;
  out->rflags = in->rflags;
}

void x86_fill_in_syscall_from_gregs(x86_syscall_general_regs_t* out,
                                    const zx_thread_state_general_regs_t* in) {
  COPY_COMMON_REGS(out, in);
  out->rip = in->rip;
  out->rsp = in->rsp;
  // Don't allow overriding privileged fields of rflags, and ignore writes
  // to reserved fields.
  out->rflags &= ~X86_FLAGS_USER;
  out->rflags |= in->rflags & X86_FLAGS_USER;
}

void x86_fill_in_gregs_from_iframe(zx_thread_state_general_regs_t* out, const x86_iframe_t* in) {
  COPY_COMMON_REGS(out, in);
  out->rsp = in->user_sp;
  out->rip = in->ip;
  out->rflags = in->flags;
}

void x86_fill_in_iframe_from_gregs(x86_iframe_t* out, const zx_thread_state_general_regs_t* in) {
  COPY_COMMON_REGS(out, in);
  out->user_sp = in->rsp;
  out->ip = in->rip;
  // Don't allow overriding privileged fields of rflags, and ignore writes
  // to reserved fields.
  out->flags &= ~X86_FLAGS_USER;
  out->flags |= in->rflags & X86_FLAGS_USER;
}

// Whether an operation gets thread state or sets it.
enum class RegAccess { kGet, kSet };

// Checks whether the mxcsr register has unsupported bits.
// The processor specifies which flags of the mxcsr are supported via the
// mxcsr_mask obtained with the fxsave instruction.
//
// The manuals mention that it is possible for the mask to be 0, and specify
// 0x000ffbf as the default value.
//
// For details see:
//   Intel 64 and IA-32 Architectures Software Developer’s Manual
//     Volume 1: Basic Architecture
//     Section: 11.6.6 Guidelines for Writing to the MXCSR Register
//   AMD64 Architecture Programmer’s Manual
//     Volume 2: System Programming
//     Section: 11.5.9  MXCSR State Management
static inline bool mxcsr_is_valid(uint32_t mxcsr, uint32_t mxcsr_mask) {
  if (mxcsr_mask == 0x0) {
    mxcsr_mask = 0x0000ffbf;
  }

  return (mxcsr & ~mxcsr_mask) == 0;
}

// Backend for arch_get_vector_regs and arch_set_vector_regs. This does a read or write of the
// thread to or from the regs structure.
zx_status_t x86_get_set_vector_regs(Thread* thread, zx_thread_state_vector_regs* regs,
                                    RegAccess access) {
  // Function to copy memory in the correct direction. Write the code using this function as if it
  // was "memcpy" in "get" mode, and it will be reversed in "set" mode.
  auto get_set_memcpy =
      (access == RegAccess::kGet)
          ? [](void* regs, void* thread, size_t size) { memcpy(regs, thread, size); }
          :                                                                           // Get mode.
          [](void* regs, void* thread, size_t size) { memcpy(thread, regs, size); };  // Set mode.

  if (access == RegAccess::kGet) {
    // Not all parts will be filled in in all cases so zero out first.
    memset(regs, 0, sizeof(zx_thread_state_vector_regs));
  }

  // Whether to force the components to be marked present in the xsave area.
  bool mark_present = access == RegAccess::kSet;

  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());

  constexpr int kNumSSERegs = 16;

  // The low 128 bits of registers 0-15 come from the legacy area and are always present.
  constexpr int kXmmRegSize = 16;  // Each XMM register is 128 bits / 16 bytes.
  uint32_t comp_size = 0;
  x86_xsave_legacy_area* save =
      static_cast<x86_xsave_legacy_area*>(x86_get_extended_register_state_component(
          thread->arch().extended_register_state, X86_XSAVE_STATE_INDEX_SSE, mark_present,
          &comp_size));
  DEBUG_ASSERT(save);  // Legacy getter should always succeed.

  // fxbug.dev/50632: Overwriting the reserved bits of the mxcsr register
  // causes a #GP Fault. We need to check against the mxcsr_mask to see if the
  // proposed mxcsr is valid.
  if (access == RegAccess::kSet && !mxcsr_is_valid(regs->mxcsr, save->mxcsr_mask)) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (int i = 0; i < kNumSSERegs; i++) {
    get_set_memcpy(&regs->zmm[i].v[0], &save->xmm[i], kXmmRegSize);
  }

  // MXCSR (always present): 32-bit status word.
  get_set_memcpy(&regs->mxcsr, &save->mxcsr, 4);

  // AVX grows the registers to 256 bits each. Optional.
  constexpr int kYmmHighSize = 16;  // Additional bytes in each register.
  uint8_t* ymm_highbits = static_cast<uint8_t*>(x86_get_extended_register_state_component(
      thread->arch().extended_register_state, X86_XSAVE_STATE_INDEX_AVX, mark_present, &comp_size));
  if (ymm_highbits) {
    DEBUG_ASSERT(comp_size == kYmmHighSize * kNumSSERegs);
    for (int i = 0; i < kNumSSERegs; i++) {
      get_set_memcpy(&regs->zmm[i].v[2], &ymm_highbits[i * kYmmHighSize], kYmmHighSize);
    }
  }

  return ZX_OK;
}

}  // namespace

zx_status_t arch_get_general_regs(Thread* thread, zx_thread_state_general_regs_t* out) {
  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());

  // Punt if registers aren't available. E.g.,
  // TODO(fxbug.dev/30521): Registers aren't available in synthetic exceptions.
  if (thread->arch().suspended_general_regs.gregs == nullptr)
    return ZX_ERR_NOT_SUPPORTED;

  DEBUG_ASSERT(thread->arch().suspended_general_regs.gregs);
  switch (GeneralRegsSource(thread->arch().general_regs_source)) {
    case GeneralRegsSource::Iframe:
      x86_fill_in_gregs_from_iframe(out, thread->arch().suspended_general_regs.iframe);
      break;
    case GeneralRegsSource::Syscall:
      x86_fill_in_gregs_from_syscall(out, thread->arch().suspended_general_regs.syscall);
      break;
    default:
      ASSERT(false);
  }

  out->fs_base = thread->arch().fs_base;
  out->gs_base = thread->arch().gs_base;

  return ZX_OK;
}

zx_status_t arch_set_general_regs(Thread* thread, const zx_thread_state_general_regs_t* in) {
  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());

  // Punt if registers aren't available. E.g.,
  // TODO(fxbug.dev/30521): Registers aren't available in synthetic exceptions.
  if (thread->arch().suspended_general_regs.gregs == nullptr)
    return ZX_ERR_NOT_SUPPORTED;

  // If these addresses are not canonical, the kernel will GPF when it tries
  // to set them as the current values.
  if (!x86_is_vaddr_canonical(in->fs_base))
    return ZX_ERR_INVALID_ARGS;
  if (!x86_is_vaddr_canonical(in->gs_base))
    return ZX_ERR_INVALID_ARGS;

  // fxbug.dev/50633: Disallow setting RIP to a non-canonical address, to
  // prevent returning to such addresses using the SYSRET or IRETQ
  // instructions. See docs/concepts/kernel/sysret_problem.md.
  //
  // The code also restricts the RIP to userspace addresses. There is no use
  // case for setting the RIP to a kernel address.
  if (!x86_is_vaddr_canonical(in->rip) || is_kernel_address(in->rip))
    return ZX_ERR_INVALID_ARGS;

  DEBUG_ASSERT(thread->arch().suspended_general_regs.gregs);
  switch (GeneralRegsSource(thread->arch().general_regs_source)) {
    case GeneralRegsSource::Iframe:
      x86_fill_in_iframe_from_gregs(thread->arch().suspended_general_regs.iframe, in);
      break;
    case GeneralRegsSource::Syscall: {
      x86_fill_in_syscall_from_gregs(thread->arch().suspended_general_regs.syscall, in);
      break;
    }
    default:
      ASSERT(false);
  }

  thread->arch().fs_base = in->fs_base;
  thread->arch().gs_base = in->gs_base;

  return ZX_OK;
}

zx_status_t arch_get_single_step(Thread* thread, zx_thread_state_single_step_t* out) {
  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());

  // Punt if registers aren't available. E.g.,
  // TODO(fxbug.dev/30521): Registers aren't available in synthetic exceptions.
  if (thread->arch().suspended_general_regs.gregs == nullptr)
    return ZX_ERR_NOT_SUPPORTED;

  uint64_t* flags = nullptr;
  switch (GeneralRegsSource(thread->arch().general_regs_source)) {
    case GeneralRegsSource::Iframe:
      flags = &thread->arch().suspended_general_regs.iframe->flags;
      break;
    case GeneralRegsSource::Syscall:
      flags = &thread->arch().suspended_general_regs.syscall->rflags;
      break;
    default:
      ASSERT(false);
  }

  *out = !!(*flags & X86_FLAGS_TF);
  return ZX_OK;
}

zx_status_t arch_set_single_step(Thread* thread, const zx_thread_state_single_step_t* in) {
  if (*in != 0 && *in != 1) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());

  // Punt if registers aren't available. E.g.,
  // TODO(fxbug.dev/30521): Registers aren't available in synthetic exceptions.
  if (thread->arch().suspended_general_regs.gregs == nullptr)
    return ZX_ERR_NOT_SUPPORTED;

  uint64_t* flags = nullptr;
  switch (GeneralRegsSource(thread->arch().general_regs_source)) {
    case GeneralRegsSource::Iframe:
      flags = &thread->arch().suspended_general_regs.iframe->flags;
      break;
    case GeneralRegsSource::Syscall:
      flags = &thread->arch().suspended_general_regs.syscall->rflags;
      break;
    default:
      ASSERT(false);
  }

  if (*in) {
    *flags |= X86_FLAGS_TF;
  } else {
    *flags &= ~X86_FLAGS_TF;
  }
  return ZX_OK;
}

zx_status_t arch_get_fp_regs(Thread* thread, zx_thread_state_fp_regs* out) {
  // Don't leak any reserved fields.
  memset(out, 0, sizeof(zx_thread_state_fp_regs));

  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());

  uint32_t comp_size = 0;
  x86_xsave_legacy_area* save =
      static_cast<x86_xsave_legacy_area*>(x86_get_extended_register_state_component(
          thread->arch().extended_register_state, X86_XSAVE_STATE_INDEX_X87, false, &comp_size));
  DEBUG_ASSERT(save);  // Legacy getter should always succeed.

  out->fcw = save->fcw;
  out->fsw = save->fsw;
  out->ftw = save->ftw;
  out->fop = save->fop;
  out->fip = save->fip;
  out->fdp = save->fdp;
  memcpy(&out->st[0], &save->st[0], sizeof(out->st));

  return ZX_OK;
}

zx_status_t arch_set_fp_regs(Thread* thread, const zx_thread_state_fp_regs* in) {
  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());

  uint32_t comp_size = 0;
  x86_xsave_legacy_area* save =
      static_cast<x86_xsave_legacy_area*>(x86_get_extended_register_state_component(
          thread->arch().extended_register_state, X86_XSAVE_STATE_INDEX_X87, true, &comp_size));
  DEBUG_ASSERT(save);  // Legacy getter should always succeed.

  save->fcw = in->fcw;
  save->fsw = in->fsw;
  save->ftw = in->ftw;
  save->fop = in->fop;
  save->fip = in->fip;
  save->fdp = in->fdp;
  memcpy(&save->st[0], &in->st[0], sizeof(in->st));

  return ZX_OK;
}

zx_status_t arch_get_vector_regs(Thread* thread, zx_thread_state_vector_regs* out) {
  return x86_get_set_vector_regs(thread, out, RegAccess::kGet);
}

zx_status_t arch_set_vector_regs(Thread* thread, const zx_thread_state_vector_regs* in) {
  // The get_set function won't write in "kSet" mode so the const_cast is safe.
  return x86_get_set_vector_regs(thread, const_cast<zx_thread_state_vector_regs*>(in),
                                 RegAccess::kSet);
}

zx_status_t arch_get_debug_regs(Thread* thread, zx_thread_state_debug_regs* out) {
  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());

  // The kernel updates this per-thread data everytime a hw debug event occurs, meaning that
  // these values will be always up to date. If the thread is not using hw debug capabilities,
  // these will have the default zero values.
  out->dr[0] = thread->arch().debug_state.dr[0];
  out->dr[1] = thread->arch().debug_state.dr[1];
  out->dr[2] = thread->arch().debug_state.dr[2];
  out->dr[3] = thread->arch().debug_state.dr[3];
  out->dr6 = thread->arch().debug_state.dr6;
  out->dr7 = thread->arch().debug_state.dr7;

  return ZX_OK;
}

zx_status_t arch_set_debug_regs(Thread* thread, const zx_thread_state_debug_regs* in) {
  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());

  // Replace the state of the thread with the given one. We now need to keep track of the debug
  // state of this register across context switches.
  x86_debug_state_t new_debug_state;
  new_debug_state.dr[0] = in->dr[0];
  new_debug_state.dr[1] = in->dr[1];
  new_debug_state.dr[2] = in->dr[2];
  new_debug_state.dr[3] = in->dr[3];
  new_debug_state.dr6 = in->dr6;
  new_debug_state.dr7 = in->dr7;

  // Validate the new input. This will mask reserved bits to their stated values.
  if (!x86_validate_debug_state(&new_debug_state))
    return ZX_ERR_INVALID_ARGS;

  // NOTE: This currently does a write-read round-trip to the CPU in order to ensure that
  //       |thread->arch().debug_state| tracks the exact value as it is stored in the registers.
  // TODO(fxbug.dev/32873): Ideally, we could do some querying at boot time about the format that
  // the CPU
  //                is storing reserved bits and we can create a mask we can apply to the input
  //                values and avoid changing the state.

  // Save the current debug state temporarily.
  x86_debug_state_t current_debug_state;
  x86_read_hw_debug_regs(&current_debug_state);

  // Write and then read from the CPU to have real values tracked by the thread data.
  // Mark the thread as now tracking the debug state.
  x86_write_hw_debug_regs(&new_debug_state);
  x86_read_hw_debug_regs(&thread->arch().debug_state);

  thread->arch().track_debug_state = true;

  // Restore the original debug state. Should always work as the input was already validated.
  x86_write_hw_debug_regs(&current_debug_state);

  return ZX_OK;
}

zx_status_t arch_get_x86_register_fs(Thread* thread, uint64_t* out) {
  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());
  *out = thread->arch().fs_base;
  return ZX_OK;
}

zx_status_t arch_set_x86_register_fs(Thread* thread, const uint64_t* in) {
  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());
  thread->arch().fs_base = *in;
  return ZX_OK;
}

zx_status_t arch_get_x86_register_gs(Thread* thread, uint64_t* out) {
  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());
  *out = thread->arch().gs_base;
  return ZX_OK;
}

zx_status_t arch_set_x86_register_gs(Thread* thread, const uint64_t* in) {
  Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

  DEBUG_ASSERT(thread->IsUserStateSavedLocked());
  thread->arch().gs_base = *in;
  return ZX_OK;
}

// NOTE: While x86 supports up to 4 hw breakpoints/watchpoints, there is a catch:
//       They are shared, so (breakpoints + watchpoints) <= HW_DEBUG_REGISTERS_COUNT.
uint8_t arch_get_hw_breakpoint_count() { return HW_DEBUG_REGISTERS_COUNT; }

uint8_t arch_get_hw_watchpoint_count() { return HW_DEBUG_REGISTERS_COUNT; }
