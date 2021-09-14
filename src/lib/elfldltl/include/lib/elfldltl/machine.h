// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MACHINE_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MACHINE_H_

#include <cstdint>
#include <optional>

#include "constants.h"

namespace elfldltl {

// This is specialized to give the machine-specific details on relocation.
template <ElfMachine Machine>
struct RelocationTraits;

// This is the prototypical specialization that serves to document the
// RelocationTraits API.  It does not correspond to an actual machine
// format; actual files with EM_NONE should not be produced or consumed
// using these relocation types.  But this can be used in unit tests.
template <>
struct RelocationTraits<ElfMachine::kNone> {
  // This lists a small subset of the relocation type codes for the machine.
  // This doesn't define each per-machine type with its own canonical name.
  // Instead it only the types used by modern dynamic linking ABIs.  Each of
  // the few types actually supported for dynamic linking has the same
  // semantics across machines, but each machine its own different name and
  // type code for each one.  This type uses a uniform set of names for these,
  // but with the actual type values each machine encodes in Elf::Rel::type().
  // The semantics associated with each type name are described below.
  //
  // In pseudo-code expressions below, these variables are used:
  //  * `Base` is the load bias of the relocated module (i.e. the difference
  //    between its runtime load address and its first PT_LOAD's p_vaddr).
  //  * `SymbolBase` is the load bias of the module defining this symbol.
  //  * `SymbolValue` is the st_value of the defining module's symbol.
  //  * `Addend` is r_addend or equivalent extracted (signed) value.
  //
  // The datum being relocated is located at Base + r_offset.

  enum class Type : uint32_t {
    // This type should never appear but has always been assigned with value
    // zero in every ABI.  Historically some linkers have occasionally produced
    // filler entries with this type that should be ignored.
    kNone,

    // These types can touch anywhere in initialized data or the GOT.
    // Theoretically they might not always be aligned in some ABIs, but this
    // implementation only supports naturally aligned relocation targets.
    // Misaligned targets cannot arise from standard C/C++ initializers, since
    // address-holding types require natural alignment in every ABI.
    kRelative,  // Base + Addend
    kAbsolute,  // SymbolBase + SymbolValue + Addend

    // GOT types do not use the addend.
    kPlt,  // SymbolBase + SymbolValue

    // This is a GOT type that stores the TLS module ID of the defining module.
    // The GOT slot is used in arguments to the ABI's runtime callback to
    // resolve thread-local references in the GD/LD TLS model.
    kTlsModule,

    // TLS "address" types use SymbolValue + Addend as the value stored.
    kTlsAbsolute,  // Relative to the thread pointer (static TLS).
    kTlsRelative,  // Relative to the symbol-defining module's TLS block.
  };

  // These types are not available on every machine and so are not included in
  // the enum.  Instead, they are defined as constexpr std::optional<uint32_t>.

  // This is like kAbsolute but without the addend, so when not doing lazy PLT
  // fixup it's exactly the same as kPlt: SymbolBase + SymbolValue.  Some
  // machines don't have a separate GOT type at all and just use kAbsolute.
  static constexpr std::optional<uint32_t> kGot = std::nullopt;

  // TLSDESC is the only type that doesn't store exactly one word.
  // It stores into two adjacent GOT slots at Base + r_offset.
  //
  // This is the modern alternative to using kTlsModule + kTlsRelative; it
  // performs better at runtime and so is always the preferred form for the
  // compiler to generate.  The first slot gets filled at runtime with the PC
  // address of a callback function.  Compiled code calls this using a special
  // calling convention that passes in the address of the GOT slots and gets
  // back a per-thread location or offset (details vary by machine, but it's a
  // bespoke convention that minimizes register spills for efficiency).  The
  // addend applies to, and for REL format is stored in, the *second* slot.
  // The runtime setup updates that slot to hold state used by its callback.
  static constexpr std::optional<uint32_t> kTlsDesc = std::nullopt;

  // TODO(fxbug.dev/84273): TLS computations
};

// Specialization for AArch64.  TODO(mcgrathr): Different types used for same
// things on ILP32, vs ELFCLASS32 LP64 wrt GOT size et al: LP64 reloc all types
// >255, don't fit in Elf32::Rel::r_info.
template <>
struct RelocationTraits<ElfMachine::kAarch64> {
  enum class Type : uint32_t {
    kNone = 0,            // R_AARCH64_NONE
    kRelative = 1027,     // R_AARCH64_RELATIVE
    kAbsolute = 257,      // R_AARCH64_ABS64
    kPlt = 1026,          // R_AARCH64_JUMP_SLOT
    kTlsAbsolute = 1030,  // R_AARCH64_TLS_TPREL64
    kTlsRelative = 1029,  // R_AARCH64_TLS_DTPREL64
    kTlsModule = 1028,    // R_AARCH64_TLS_DTPMOD64
  };
  static constexpr std::optional<uint32_t> kGot = 1025;      // R_AARCH64_GLOB_DAT
  static constexpr std::optional<uint32_t> kTlsDesc = 1031;  // R_AARCH64_TLSDESC
};

// Specialization for x86-64.
template <>
struct RelocationTraits<ElfMachine::kX86_64> {
  enum class Type : uint32_t {
    kNone = 0,          // R_X86_64_NONE
    kRelative = 8,      // R_X86_64_RELATIVE
    kAbsolute = 1,      // R_X86_64_64
    kPlt = 7,           // R_X86_64_JUMP_SLOT
    kTlsAbsolute = 18,  // R_X86_64_TPOFF64
    kTlsRelative = 17,  // R_X86_64_DTPOFF64
    kTlsModule = 16,    // R_X86_64_DTPMOD64

  };
  static constexpr std::optional<uint32_t> kGot = 6;       // R_X86_64_GLOB_DAT
  static constexpr std::optional<uint32_t> kTlsDesc = 36;  // R_X86_64_TLSDESC
};

// Specialization for i386.
template <>
struct RelocationTraits<ElfMachine::k386> {
  enum class Type : uint32_t {
    kNone = 0,          // R_386_NONE
    kRelative = 8,      // R_386_RELATIVE
    kAbsolute = 1,      // R_386_64
    kPlt = 7,           // R_386_JUMP_SLOT
    kTlsAbsolute = 37,  // R_386_TPOFF32
    kTlsRelative = 36,  // R_386_DTPOFF32
    kTlsModule = 35,    // R_386_DTPMOD32
  };
  static constexpr std::optional<uint32_t> kGot = 6;       // R_386_GLOB_DAT
  static constexpr std::optional<uint32_t> kTlsDesc = 41;  // R_386_TLS_DESC
};

// Specialization for RISCV.
template <>
struct RelocationTraits<ElfMachine::kRiscv> {
  enum class Type : uint32_t {
    kNone = 0,          // R_RISCV_NONE
    kRelative = 3,      // R_RISCV_RELATIVE
    kAbsolute = 2,      // R_RISCV_64
    kGot = kAbsolute,   // R_RISCV_64
    kPlt = 5,           // R_RISCV_JUMP_SLOT
    kTlsAbsolute = 10,  // R_RISCV_TPRELF64
    kTlsRelative = 9,   // R_RISCV_DTPREL64
    kTlsModule = 7,     // R_RISCV_DTPMOD64
  };

  // RISCV doesn't have a separate GOT type, since the semantics are the same
  // as kAbsolute anyway.
  static constexpr std::optional<uint32_t> kGot = std::nullopt;

  // RISCV is unfortunately missing TLSDESC.
  // https://github.com/riscv-non-isa/riscv-elf-psabi-doc/issues/94 tracks
  // getting it specified.
  static constexpr std::optional<uint32_t> kTlsDesc = std::nullopt;
};

// This should list all the fully-defined specializations except for kNone.
template <template <ElfMachine...> class Template>
using AllSupportedMachines = Template<  //
    ElfMachine::kAarch64, ElfMachine::kX86_64, ElfMachine::k386, ElfMachine::kRiscv>;

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MACHINE_H_
