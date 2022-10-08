// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// These declarations relate to the TLSDESC runtime ABI.  While the particular
// ABI details are specific to each machine, they all fit a common pattern.
//
// The R_*_TLSDESC relocation type directs dynamic linking to fill in a special
// pair of adjacent GOT slots.  The first slot is unfilled at link time and
// gets the PC of a special function provided by the dynamic linking runtime.
// For each TLS reference, the compiler generates an indirect call via this GOT
// slot.  The compiler also passes that address in the GOT to that function.
//
// This is a normal indirect call at the machine level.  However, it uses its
// own bespoke calling convention specified in the psABI for each machine
// rather than the standard C/C++ calling convention.  The convention for each
// machine is similar: the use of the return address register and/or stack is
// normal; one or two registers are designated for the argument (GOT address),
// return value, and scratch; all other registers are preserved by the call,
// except the condition codes.  The return value is usually either an address
// or an offset from the psABI-specified thread-pointer register.
//
// This makes the impact of the runtime call on code generation very minimal.
// The runtime implementation both can refer to the value stored in the GOT
// slot by dynamic linking and can in theory dynamically update both slots to
// lazily redirect to a different runtime entry point and argument data.
//
// The relocation's symbol and addend are meant to apply to the second GOT slot
// of the pair.  (For DT_REL format, the addend is stored in place there.)
// When dynamic linking chooses an entry point to store into the first GOT slot
// it also chooses the value to store in the second slot, which is some kind of
// offset or address that includes the addend and symbol value calculations.

#ifdef __ASSEMBLER__  // clang-format off

// Given standard .cfi_startproc initial state, reset CFI to indicate the
// special ABI for the R_*_TLSDESC callback function on this machine.

.macro .cfi.tlsdesc

#if defined(__aarch64__)

  // Almost all registers are preserved from the caller.  The integer set does
  // not include x30 (LR) or SP, which .cfi_startproc covered.
  .cfi.all_integer .cfi.same_value
  .cfi.all_vectorfp .cfi.same_value

  // On entry x0 contains the argument: the address of the GOT slot pair.
  // On exit x0 contains the return value: offset from $tp (TPIDR_EL0).
  .cfi_undefined x0

#elif defined(__x86_64__)

  // Almost all registers are preserved from the caller.  The integer set does
  // not include %rsp, which .cfi_startproc covered.
  .cfi.all_integer .cfi.same_value
  .cfi.all_vectorfp .cfi.same_value

  // On entry %rax contains the argument: the address of the GOT slot pair.
  // On exit %rax contains the return value: offset from $tp (%fs.base).
  .cfi_undefined %rax

#else

// Not all machines have TLSDESC support specified in the psABI.

#endif

.endm

#else  // clang-format on

#include <cstdint>

namespace [[gnu::visibility("hidden")]] ld {

// When the compiler generates a TLSDESC-style reference to a TLS variable, it
// loads a designated register with the address of a pair of GOT slots.  A
// single R_*_TLSDESC_* dynamic relocation refers to the pair.  The addend
// applies to (and for DT_REL format, is stored in) the second slot.  The
// first slot is initialized at load time to a PC address to be called with
// the address of the GOT (first) entry in the designated register (using an
// otherwise bespoke calling convention rather than the machine's norm).
struct TlsDescGot {
  uintptr_t call;
  uintptr_t offset;
};

// This is a callback function to be used in the TlsDescGot::call slot at
// runtime.  Though it's declared here as a C++ function with an argument, it's
// actually implemented in assembly code with a bespoke calling convention for
// the argument, return value, and register usage that's different from normal
// functions, so this cannot actually be called from C++.  This symbol name is
// not visible anywhere outside the dynamic linking implementation itself and
// the function is only ever called by compiler-generated TLSDESC references.
//
// In this minimal implementation used for PT_TLS segments in the static TLS
// set, got.offset is always simply a fixed offset from the thread pointer.
// Note this offset might be negative, but it's always handled as uintptr_t to
// ensure well-defined overflow arithmetic.

extern "C" uintptr_t _ld_tlsdesc_runtime_static(const TlsDescGot& got);

}  // namespace gnu::visibility("hidden")]]ld

#endif  // __ASSEMBLER__
