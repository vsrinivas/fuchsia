// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_VMX_STATE_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_VMX_STATE_H_

#include <zircon/compiler.h>

#define VS_RESUME 0

#define HS_RSP (VS_RESUME + 8)
#define HS_XCR0 (HS_RSP + 8)

#define GS_RAX (HS_XCR0 + 8)
#define GS_RCX (GS_RAX + 8)
#define GS_RDX (GS_RCX + 8)
#define GS_RBX (GS_RDX + 8)
#define GS_RBP (GS_RBX + 8)
#define GS_RSI (GS_RBP + 8)
#define GS_RDI (GS_RSI + 8)
#define GS_R8 (GS_RDI + 8)
#define GS_R9 (GS_R8 + 8)
#define GS_R10 (GS_R9 + 8)
#define GS_R11 (GS_R10 + 8)
#define GS_R12 (GS_R11 + 8)
#define GS_R13 (GS_R12 + 8)
#define GS_R14 (GS_R13 + 8)
#define GS_R15 (GS_R14 + 8)
#define GS_CR2 (GS_R15 + 8)

#ifndef __ASSEMBLER__

#include <bits.h>
#include <zircon/types.h>

// Holds the register state used to restore a host.
struct HostState {
  // Host stack pointer.
  uint64_t rsp;

  // Extended control registers.
  uint64_t xcr0;
};

struct GuestState {
  //  RIP, RSP, and RFLAGS are automatically saved by VMX in the VMCS.
  uint64_t rax;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rbx;
  uint64_t rbp;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;

  // Control registers.
  uint64_t cr2;

  // Extended control registers.
  uint64_t xcr0;

  // Convenience getters for accessing low 32-bits of common registers.
  uint32_t eax() const { return static_cast<uint32_t>(rax); }
  uint32_t ecx() const { return static_cast<uint32_t>(rcx); }
  uint32_t edx() const { return static_cast<uint32_t>(rdx); }
  uint32_t ebx() const { return static_cast<uint32_t>(rbx); }

  // Convenience getter/setter for fetching the 64-bit value edx:eax, used by
  // several x86_64 instructions, such as `rdmsr` and `wrmsr`.
  //
  // For reads, the top bits of rax and rdx are ignored (c.f. Volume 2C,
  // WRMSR). For writes, the top bits of rax and rdx are set to zero, matching
  // the behaviour of x86_64 instructions such as `rdmsr` (c.f. Volume 2C,
  // RDMSR).
  uint64_t EdxEax() const { return static_cast<uint64_t>(edx()) << 32 | eax(); }
  void SetEdxEax(uint64_t value) {
    rax = BITS_SHIFT(value, 31, 0);
    rdx = BITS_SHIFT(value, 63, 32);
  }
};

struct VmxState {
  bool resume;
  HostState host_state;
  GuestState guest_state;
};

static_assert(__offsetof(VmxState, resume) == VS_RESUME);

static_assert(__offsetof(VmxState, host_state.rsp) == HS_RSP);
static_assert(__offsetof(VmxState, host_state.xcr0) == HS_XCR0);

static_assert(__offsetof(VmxState, guest_state.rax) == GS_RAX);
static_assert(__offsetof(VmxState, guest_state.rbx) == GS_RBX);
static_assert(__offsetof(VmxState, guest_state.rcx) == GS_RCX);
static_assert(__offsetof(VmxState, guest_state.rdx) == GS_RDX);
static_assert(__offsetof(VmxState, guest_state.rdi) == GS_RDI);
static_assert(__offsetof(VmxState, guest_state.rsi) == GS_RSI);
static_assert(__offsetof(VmxState, guest_state.rbp) == GS_RBP);
static_assert(__offsetof(VmxState, guest_state.r8) == GS_R8);
static_assert(__offsetof(VmxState, guest_state.r9) == GS_R9);
static_assert(__offsetof(VmxState, guest_state.r10) == GS_R10);
static_assert(__offsetof(VmxState, guest_state.r11) == GS_R11);
static_assert(__offsetof(VmxState, guest_state.r12) == GS_R12);
static_assert(__offsetof(VmxState, guest_state.r13) == GS_R13);
static_assert(__offsetof(VmxState, guest_state.r14) == GS_R14);
static_assert(__offsetof(VmxState, guest_state.r15) == GS_R15);
static_assert(__offsetof(VmxState, guest_state.cr2) == GS_CR2);

// Launch/resume the guest, and return when the guest next exits.
//
// If we return ZX_OK, the guest was successfully launched and has now
// exited again. Otherwise, we failed to launch the guest.
zx_status_t vmx_enter(VmxState* vmx_state);

// Implemented in assembly.
extern "C" {

// Low-level functionality to save register and restore register state
// before/after entering a guest. Should only be called by vmx_enter.
zx_status_t vmx_enter_asm(VmxState* vmx_state);

// The location where we jump to when a guest exits. An internal implementation
// detail of vmx_enter_asm().
void vmx_guest_exit();

}  // extern C

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_VMX_STATE_H_
