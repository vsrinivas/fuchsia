// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/version.h>
#include <string.h>
#include <zircon/assert.h>

#include <cstdint>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <lk/init.h>

#include <profile/InstrProfData.inc>

namespace {

using IntPtrT = intptr_t;

enum ValueKind {
#define VALUE_PROF_KIND(Enumerator, Value, Descr) Enumerator = Value,
#include <profile/InstrProfData.inc>
};

struct __llvm_profile_data {
#define INSTR_PROF_DATA(Type, LLVMType, Name, Initializer) Type Name;
#include <profile/InstrProfData.inc>
};

extern "C" {

// This is sometimes emitted by the compiler with a different value.
// The header is expected to use whichever value this had at link time.
// This supplies the default value when the compiler doesn't supply it.
[[gnu::weak]] extern const uint64_t INSTR_PROF_RAW_VERSION_VAR = INSTR_PROF_RAW_VERSION;

// The compiler emits phantom references to this as a way to ensure
// that the runtime is linked in.
extern const int INSTR_PROF_PROFILE_RUNTIME_VAR = 0;

}  // extern "C"

// Here _WIN32 really means EFI (Gigaboot).  At link-time, it's Windows/x64
// essentially.  InstrProfData.inc uses #ifdef _WIN32, so match that.
#ifdef _WIN32

// These magic section names don't have macros in InstrProfData.inc,
// though their ".blah$M" counterparts do.

// Merge read-write sections into .data.
#pragma comment(linker, "/MERGE:.lprfc=.data")
#pragma comment(linker, "/MERGE:.lprfd=.data")

// Do not merge .lprfn and .lcovmap into .rdata.
// `llvm-cov` must be able to find them after the fact.

// Allocate read-only section bounds.
#pragma section(".lprfn$A", read)
#pragma section(".lprfn$Z", read)

// Allocate read-write section bounds.
#pragma section(".lprfd$A", read, write)
#pragma section(".lprfd$Z", read, write)
#pragma section(".lprfc$A", read, write)
#pragma section(".lprfc$Z", read, write)

// The ".blah$A" and ".blah$Z" dummy sections get magically sorted
// with ".blah$M" in between them, so these symbols identify the
// bounds of the compiler-emitted data at link time.  The all-zero
// dummy records don't matter to `llvm-profdata`.

// This data is morally `const`, i.e. it's a RELRO case in the ELF world.
// But the compiler complains about a mismatch with the #pragma section
// above if these are declared `const` in the PE-COFF case.
[[gnu::section(".lprfd$A"), gnu::used]] __llvm_profile_data DataStart{};
[[gnu::section(".lprfd$Z"), gnu::used]] __llvm_profile_data DataEnd{};

[[gnu::section(".lprfn$A"), gnu::used]] const char NamesStart{};
[[gnu::section(".lprfn$Z"), gnu::used]] const char NamesEnd{};

[[gnu::section(".lprfc$A"), gnu::used]] uint64_t CountersStart{};
[[gnu::section(".lprfc$Z"), gnu::used]] uint64_t CountersEnd{};

#else  // !_WIN32

extern "C" {

extern const __llvm_profile_data DataStart __asm__(
    INSTR_PROF_QUOTE(INSTR_PROF_SECT_START(INSTR_PROF_DATA_COMMON)));
extern const __llvm_profile_data DataEnd __asm__(
    INSTR_PROF_QUOTE(INSTR_PROF_SECT_STOP(INSTR_PROF_DATA_COMMON)));

extern const char NamesStart __asm__(
    INSTR_PROF_QUOTE(INSTR_PROF_SECT_START(INSTR_PROF_NAME_COMMON)));
extern const char NamesEnd __asm__(INSTR_PROF_QUOTE(INSTR_PROF_SECT_STOP(INSTR_PROF_NAME_COMMON)));

extern uint64_t CountersStart __asm__(
    INSTR_PROF_QUOTE(INSTR_PROF_SECT_START(INSTR_PROF_CNTS_COMMON)));
extern uint64_t CountersEnd __asm__(INSTR_PROF_QUOTE(INSTR_PROF_SECT_STOP(INSTR_PROF_CNTS_COMMON)));

}  // extern "C"

#endif  // _WIN32

// These are used by the INSTR_PROF_RAW_HEADER initializers.

constexpr uint64_t __llvm_profile_get_magic() { return INSTR_PROF_RAW_MAGIC_64; }

uint64_t __llvm_profile_get_version() { return INSTR_PROF_RAW_VERSION_VAR; }

// TODO(fxbug.dev/81362): this is used by the InstrProfData.inc code in the new
// version but not the old.  Remove this attribute after the new toolchain has
// landed.
[[gnu::unused]]
uint64_t __llvm_write_binary_ids(void* ignored) { return sizeof(uint64_t) + ElfBuildId().size(); }

#define DataSize (&DataEnd - &DataStart)
#define PaddingBytesBeforeCounters 0
#define CountersSize (&CountersEnd - &CountersStart)
#define PaddingBytesAfterCounters 0
#define NamesSize (&NamesEnd - &NamesStart)
#define CountersBegin (reinterpret_cast<uint64_t>(&CountersStart))
#define DataBegin (reinterpret_cast<uint64_t>(&DataStart))
#define NamesBegin (reinterpret_cast<uint64_t>(&NamesStart))

// The linker script places this at the start of a page-aligned region
// where it's followed by the compiler-generated sections.
[[gnu::section("__llvm_profile_header"), gnu::used]] struct {
#define INSTR_PROF_RAW_HEADER(Type, Name, Initializer) Type Name##_ = Initializer;
#include <profile/InstrProfData.inc>
} __llvm_profile_header;

// Defined by the kernel.ld linker script (which see).
extern "C" const uint64_t __llvm_profile_build_id_size;
extern "C" ktl::byte __llvm_profile_build_id_bytes[];

// The build ID is embedded by the linker in one place, but we need it also
// embedded in a second place to form part of the profdata format.  Even though
// our profdata format layout is done at link time via the linker script, the
// linker won't fill in the build ID contents in two places, only in the ELF
// note.  So we have to update the profdata image in place.  The kernel.ld bits
// that place the header already filled in the size of the build ID at link
// time, right after the header, and then left a gap of that many bytes for the
// build ID to be filled in.
void InitLlvmProfileBuildId(unsigned int level) {
  ktl::span<const ktl::byte> link_build_id = ElfBuildId();
  ktl::span<ktl::byte> profdata_build_id{__llvm_profile_build_id_bytes,
                                         __llvm_profile_build_id_size};
  ZX_ASSERT(profdata_build_id.size_bytes() == link_build_id.size_bytes());
  memcpy(profdata_build_id.data(), link_build_id.data(), link_build_id.size_bytes());
}

}  // namespace

LK_INIT_HOOK(InitLlvmProfileBuildId, InitLlvmProfileBuildId, LK_INIT_LEVEL_ARCH_LATE + 1)
