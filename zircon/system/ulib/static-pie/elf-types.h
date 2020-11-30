// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_SYSTEM_ULIB_STATIC_PIE_ELF_TYPES_H_
#define ZIRCON_SYSTEM_ULIB_STATIC_PIE_ELF_TYPES_H_

#include <cstdint>

#include <hwreg/bitfields.h>

namespace static_pie {

// Relocation types.
//
// Architecture-independent relocation types.
enum class ElfRelocType : uint32_t {
  kNone = 0,
#if defined(__x86_64__)
  kRelative = 8,  // R_X86_64_RELATIVE
#elif defined(__aarch64__)
  kRelative = 1027,  // R_AARCH64_RELATIVE
#else
#error "Unknown architecture."
#endif
};

// Tags used in the ".dynamic" table.
//
// c.f. "Dynamic Section", Chapter 13, _Linker and Libraries Guide_,
// Oracle, November 2011.
//
// Relr entries match proposal at
// <https://groups.google.com/g/generic-abi/c/bX460iggiKg/m/YT2RrjpMAwAJ>.
enum DynamicArrayTag : uint64_t {
  kNull = 0,                // DT_NULL: last element of the array.
  kRela = 7,                // DT_RELA: Address of the Rela table.
  kRelaSize = 8,            // DT_RELASZ: Size of the Rela table, in bytes.
  kRelaEntrySize = 9,       // DT_RELAENT: Size of one Rela entry, in bytes.
  kRel = 17,                // DT_REL: Address of the Rel table.
  kRelSize = 18,            // DT_RELSZ: Size of the Rel table, in bytes.
  kRelEntrySize = 19,       // DT_RELENT: Size of one Rel entry, in bytes.
  kRelrSize = 35,           // DT_RELRSZ: Size of the Relr table, in bytes.
  kRelr = 36,               // DT_RELR: Address of the Relr table.
  kRelrEntrySize = 37,      // DT_RELRENT: Size of one Relr entry, in bytes.
  kRelaCount = 0x6ffffff9,  // DT_RELACOUNT: Number of RELATIVE relocations in the Rela table.
  kRelCount = 0x6ffffffa,   // DT_RELCOUNT: Number of RELATIVE relocations in the Rel table.
};

// Bits for the `info` field of the Rel and Rela entries.
struct Elf64RelInfo {
  uint64_t data;

  // The symbol index for this field.
  DEF_SUBFIELD(data, 63, 32, symbol);

  // The relocation type.
  DEF_ENUM_SUBFIELD(data, ElfRelocType, 31, 0, type);

  // Generate a Elf64RelInfo of the given type, with symbol set to 0.
  static constexpr Elf64RelInfo OfType(ElfRelocType type) {
    Elf64RelInfo info{};
    info.set_type(type);
    return info;
  }
};

// 64-bit ELF SHT_REL relocation entry.
//
// c.f. "Relocation Sections", Chapter 12, _Linker and Libraries Guide_,
// Oracle, November 2011.
struct Elf64RelEntry {
  // Virtual address to patch.
  //
  // For position-independent executables, the virtual addresses of the
  // first PT_LOAD segment will typically be (but are not guaranteed to
  // be) 0 prior to relocation.
  uint64_t offset;

  // Relocation to apply.
  Elf64RelInfo info;
};

// 64-bit ELF SHT_RELA relocation entry.
//
// c.f. "Relocation Sections", Chapter 12, _Linker and Libraries Guide_,
// Oracle, November 2011.
struct Elf64RelaEntry {
  uint64_t offset;    // Offset to patch, relative to the beginning of the storage unit.
  Elf64RelInfo info;  // Relocation details.
  uint64_t addend;    // Relocation value. The interpretation of this is relocation-specific.
};

// An entry in the ".dynamic" table.
//
// c.f. "Dynamic Section", Chapter 13, _Linker and Libraries Guide_,
// Oracle, November 2011.
struct Elf64DynamicEntry {
  DynamicArrayTag tag;
  uint64_t value;
};

}  // namespace static_pie

#endif  // ZIRCON_SYSTEM_ULIB_STATIC_PIE_ELF_TYPES_H_
