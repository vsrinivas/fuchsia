// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_CONSTANTS_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_CONSTANTS_H_

#include <lib/stdcompat/bit.h>

#include <cstdint>

namespace elfldltl {

// Some header fields have uniform bit values across all kinds of ELF files.
// These are declared here at top level.

// The bit width (32-bit vs 64-bit) is called the "ELF class".
enum class ElfClass : uint8_t {
  k32 = 1,
  k64 = 2,
  kNative =
      []() {
        if constexpr (sizeof(uintptr_t) == sizeof(uint64_t)) {
          return k64;
        } else if constexpr (sizeof(uintptr_t) == sizeof(uint32_t)) {
          return k32;
        }
      }()
};

// The byte order (Least Significant Byte first, aka little-endian, vs
// Most Significant Byte first, aka big-endian) used in ELF metadata.
// All fields are stored as two's complement integers, hence the names.
enum class ElfData : uint8_t {
  k2Lsb = 1,
  k2Msb = 2,
  kNative =
      []() {
        if constexpr (cpp20::endian::native == cpp20::endian::little) {
          return k2Lsb;
        } else if constexpr (cpp20::endian::native == cpp20::endian::big) {
          return k2Msb;
        }
      }()
};

// This is just a fixed constant that cannot vary.
enum class ElfVersion : uint8_t { kCurrent = 1 };

// This indicates the type of ELF file, found in Elf::Ehdr::type().  Only
// ET_DYN is handled at runtime but the others are provided for the convenience
// of other tools.
enum class ElfType : uint16_t {
  kNone = 0,
  kRel = 1,
  kExec = 2,
  kDyn = 3,
  kCore = 4,
};

// These are the types of program headers, found in Elf::Phdr::type().
// This lists only the types used at runtime.
enum class ElfPhdrType : uint32_t {
  kNull = 0,
  kLoad = 1,
  kDynamic = 2,
  kInterp = 3,
  kNote = 4,
  kTls = 7,
  kEhFrameHdr = 0x6474e550,  // PT_GNU_EH_FRAME
  kStack = 0x6474e551,       // PT_GNU_STACK
  kRelro = 0x6474e552,       // PT_GNU_RELRO
};

// These are the types of section headers, found in Elf::Shdr::type().
enum class ElfShdrType : uint32_t {
  kNull = 0,
  kProgbits = 1,
  kSymtab = 2,
  kStrtab = 3,
  kRela = 4,
  kHash = 5,
  kDynamic = 6,
  kNote = 7,
  kNobits = 8,
  kRel = 9,
  kShlib = 10,
  kDynsym = 11,
  kInitArray = 14,
  kFiniArray = 15,
  kPreinitArray = 16,
  kGroup = 17,
  kSymtabShndx = 18,
  kGnuAttributes = 0x6ffffff5,
  kGnuHash = 0x6ffffff6,
  kGnuLiblist = 0x6ffffff7,
  kChecksum = 0x6ffffff8,
  kSunwMove = 0x6ffffffa,
  kSunwComdat = 0x6ffffffb,
  kSunwSyminfo = 0x6ffffffc,
  kGnuVerdef = 0x6ffffffd,
  kGnuVerneed = 0x6ffffffe,
  kGnuVersym = 0x6fffffff,
};

// These are the PT_DYNAMIC entry tags, found in Elf::Dyn::tag().
enum class ElfDynTag : uint32_t {
  kNull = 0,
  kNeeded = 1,
  kPltRelSz = 2,
  kPltGot = 3,
  kHash = 4,
  kStrTab = 5,
  kSymTab = 6,
  kRela = 7,
  kRelaSz = 8,
  kRelaEnt = 9,
  kStrSz = 10,
  kSymEnt = 11,
  kInit = 12,
  kFini = 13,
  kSoname = 14,
  kRpath = 15,
  kSymbolic = 16,
  kRel = 17,
  kRelSz = 18,
  kRelEnt = 19,
  kPltRel = 20,
  kDebug = 21,
  kTextRel = 22,
  kJmpRel = 23,
  kBindNow = 24,
  kInitArray = 25,
  kFiniArray = 26,
  kInitArraySz = 27,
  kFiniArraySz = 28,
  kRunPath = 29,
  kFlags = 30,
  kPreinitArray = 32,
  kPreinitArraySz = 33,
  kSymTabShndx = 34,
  kRelrSz = 35,
  kRelr = 36,
  kRelrEnt = 37,
  kFeature1 = 0x6ffffdfc,
  kGnuHash = 0x6ffffef5,
  kTlsDescPlt = 0x6ffffef6,
  kTlsDescGot = 0x6ffffef7,
  kRelaCount = 0x6ffffff9,
  kRelCount = 0x6ffffffa,
  kFlags1 = 0x6ffffffb,
};

// These are the "binding" classes of symbols, found in Elf::Sym::bind().
enum class ElfSymBind : uint8_t {
  kLocal = 0,
  kGlobal = 1,
  kWeak = 2,
  kUnique = 10,  // STB_GNU_UNIQUE is a GNU extension not widely supported.
};

// These are the types of symbols, found in Elf::Sym::type().
enum class ElfSymType : uint8_t {
  kNoType = 0,
  kObject = 1,
  kFunc = 2,
  kSection = 3,
  kFile = 4,
  kCommon = 5,
  kTls = 6,
  kIfunc = 10,  // STT_GNU_IFUNC is a GNU extension not widely supported.
};

// This indicates the machine architecture the ELF file is for, as found in
// Elf::Ehdr::machine().  There are many more EM_* constants specified by ELF.
// This lists only those for which the library provides some degree of support.
enum class ElfMachine : uint16_t {
  kNone = 0,
  k386 = 3,
  kX86_64 = 62,
  kAarch64 = 183,
  kRiscv = 243,

  kNative =
      []() {
#ifdef __aarch64__
        return kAarch64;
#elif defined(__i386__)
        return k386;
#elif defined(__x86_64__)
        return kX86_64;
#elif defined(__riscv)
        return kRiscv;
#endif
        return kNone;
      }()
};

// These are types used in notes.  Other types might appear in note headers.
// Only those used by the library are listed here.
enum class ElfNoteType : uint32_t {
  // These use name "GNU".
  kGnuBuildId = 3,
  kGnuPropertyType0 = 5,
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_CONSTANTS_H_
