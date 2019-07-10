// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <llvm/ProfileData/InstrProfData.inc>

namespace {

using IntPtrT = intptr_t;

enum ValueKind {
#define VALUE_PROF_KIND(Enumerator, Value) Enumerator = Value,
#include <llvm/ProfileData/InstrProfData.inc>
};

struct __llvm_profile_data {
#define INSTR_PROF_DATA(Type, LLVMType, Name, Initializer) Type Name;
#include <llvm/ProfileData/InstrProfData.inc>
};

extern "C" {

// This is sometimes emitted by the compiler with a different value.
// The header is expected to use whichever value this had at link time.
// This supplies the default value when the compiler doesn't supply it.
[[gnu::weak]] extern const uint64_t INSTR_PROF_RAW_VERSION_VAR =
    INSTR_PROF_RAW_VERSION;

// The compiler emits phantom references to this as a way to ensure
// that the runtime is linked in.
extern const int INSTR_PROF_PROFILE_RUNTIME_VAR = 0;

#define PROF_DATA_START INSTR_PROF_SECT_START(INSTR_PROF_DATA_COMMON)
#define PROF_DATA_STOP INSTR_PROF_SECT_STOP(INSTR_PROF_DATA_COMMON)
extern const __llvm_profile_data PROF_DATA_START[], PROF_DATA_STOP[];

#define PROF_NAME_START INSTR_PROF_SECT_START(INSTR_PROF_NAME_COMMON)
#define PROF_NAME_STOP INSTR_PROF_SECT_STOP(INSTR_PROF_NAME_COMMON)
extern const char PROF_NAME_START[], PROF_NAME_STOP[];

#define PROF_CNTS_START INSTR_PROF_SECT_START(INSTR_PROF_CNTS_COMMON)
#define PROF_CNTS_STOP INSTR_PROF_SECT_STOP(INSTR_PROF_CNTS_COMMON)
extern const uint64_t PROF_CNTS_START[], PROF_CNTS_STOP[];

} // extern "C"

// These are used by the INSTR_PROF_RAW_HEADER initializers.

constexpr uint64_t __llvm_profile_get_magic() {
    return INSTR_PROF_RAW_MAGIC_64;
}

uint64_t __llvm_profile_get_version() {
    return INSTR_PROF_RAW_VERSION_VAR;
}

#define DataSize (PROF_DATA_STOP - PROF_DATA_START)
#define CountersSize (PROF_CNTS_STOP - PROF_CNTS_START)
#define NamesSize (PROF_NAME_STOP - PROF_NAME_START)
#define CountersBegin (reinterpret_cast<uint64_t>(PROF_CNTS_START))
#define NamesBegin (reinterpret_cast<uint64_t>(PROF_NAME_START))

// The linker script places this at the start of a page-aligned region
// where it's followed by the compiler-generated sections.
[[gnu::section("__llvm_profile_header"), gnu::used]] struct {
#define INSTR_PROF_RAW_HEADER(Type, Name, Initializer) \
    Type Name##_ = Initializer;
#include <llvm/ProfileData/InstrProfData.inc>
} __llvm_profile_header;

} // namespace
