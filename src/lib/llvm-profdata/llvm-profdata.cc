// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/llvm-profdata/llvm-profdata.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#ifndef HAVE_PROFDATA
#error "build system regression"
#endif

#if !HAVE_PROFDATA

// If not compiled with instrumentation at all, then all the link-time
// references in the real implementation below won't work.  So provide stubs.

void LlvmProfdata::Init(cpp20::span<const std::byte> build_id) {}

cpp20::span<std::byte> LlvmProfdata::DoFixedData(cpp20::span<std::byte> data, bool match) {
  return {};
}

void LlvmProfdata::CopyCounters(cpp20::span<std::byte> data) {}

void LlvmProfdata::MergeCounters(cpp20::span<std::byte> data) {}

void LlvmProfdata::UseCounters(cpp20::span<std::byte> data) {}

#else  // HAVE_PROFDATA

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <profile/InstrProfData.inc>

namespace {

constexpr uint64_t kMagic = INSTR_PROF_RAW_MAGIC_64;

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

// In relocating mode, the compiler adds this to the address of a profiling
// counter in .bss for the counter to actually update.  At startup, this is
// zero so the .bss counters get updated.  When data is being published, the
// live-published counters get copied from the .bss counters and then this is
// set so future updates are redirected to the published copy.
//
// This definition is weak in case the standard profile runtime is also linked
// in with its own definition.
[[gnu::weak]] extern intptr_t INSTR_PROF_PROFILE_COUNTER_BIAS_VAR = 0;

}  // extern "C"

// Here _WIN32 really means EFI (Gigaboot).  At link-time, it's Windows/x64
// essentially.  InstrProfData.inc uses #ifdef _WIN32, so match that.
#if defined(_WIN32)

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

// The ".blah$A" and ".blah$Z" placeholder sections get magically sorted with
// ".blah$M" in between them, so these symbols identify the bounds of the
// compiler-emitted data at link time.  The all-zero placeholder records don't
// matter to `llvm-profdata`.

// This data is morally `const`, i.e. it's a RELRO case in the ELF world.
// But the compiler complains about a mismatch with the #pragma section
// above if these are declared `const` in the PE-COFF case.
[[gnu::visibility("hidden"), gnu::section(".lprfd$A"), gnu::used]] __llvm_profile_data DataBegin[];
[[gnu::visibility("hidden"), gnu::section(".lprfd$Z"), gnu::used]] __llvm_profile_data DataEnd[];

[[gnu::visibility("hidden"), gnu::section(".lprfn$A"), gnu::used]] const char NamesBegin[];
[[gnu::visibility("hidden"), gnu::section(".lprfn$Z"), gnu::used]] const char NamesEnd[];

[[gnu::visibility("hidden"), gnu::section(".lprfc$A"), gnu::used]] uint64_t CountersBegin[];
[[gnu::visibility("hidden"), gnu::section(".lprfc$Z"), gnu::used]] uint64_t CountersEnd[];

#elif defined(__APPLE__)

extern "C" {

[[gnu::visibility("hidden")]] extern const __llvm_profile_data DataBegin[] __asm__(
    "section$start$__DATA$" INSTR_PROF_DATA_SECT_NAME);
[[gnu::visibility("hidden")]] extern const __llvm_profile_data DataEnd[] __asm__(
    "section$end$__DATA$" INSTR_PROF_DATA_SECT_NAME);

[[gnu::visibility("hidden")]] extern const char NamesBegin[] __asm__(
    "section$start$__DATA$" INSTR_PROF_NAME_SECT_NAME);
[[gnu::visibility("hidden")]] extern const char NamesEnd[] __asm__(
    "section$end$__DATA$" INSTR_PROF_NAME_SECT_NAME);

[[gnu::visibility("hidden")]] extern uint64_t CountersBegin[] __asm__(
    "section$start$__DATA$" INSTR_PROF_CNTS_SECT_NAME);
[[gnu::visibility("hidden")]] extern uint64_t CountersEnd[] __asm__(
    "section$end$__DATA$" INSTR_PROF_CNTS_SECT_NAME);

}  // extern "C"

#else  // Not _WIN32 or __APPLE__.

extern "C" {

[[gnu::visibility("hidden")]] extern const __llvm_profile_data DataBegin[] __asm__(
    INSTR_PROF_QUOTE(INSTR_PROF_SECT_START(INSTR_PROF_DATA_COMMON)));
[[gnu::visibility("hidden")]] extern const __llvm_profile_data DataEnd[] __asm__(
    INSTR_PROF_QUOTE(INSTR_PROF_SECT_STOP(INSTR_PROF_DATA_COMMON)));

[[gnu::visibility("hidden")]] extern const char NamesBegin[] __asm__(
    INSTR_PROF_QUOTE(INSTR_PROF_SECT_START(INSTR_PROF_NAME_COMMON)));
[[gnu::visibility("hidden")]] extern const char NamesEnd[] __asm__(
    INSTR_PROF_QUOTE(INSTR_PROF_SECT_STOP(INSTR_PROF_NAME_COMMON)));

[[gnu::visibility("hidden")]] extern uint64_t CountersBegin[] __asm__(
    INSTR_PROF_QUOTE(INSTR_PROF_SECT_START(INSTR_PROF_CNTS_COMMON)));
[[gnu::visibility("hidden")]] extern uint64_t CountersEnd[] __asm__(
    INSTR_PROF_QUOTE(INSTR_PROF_SECT_STOP(INSTR_PROF_CNTS_COMMON)));

}  // extern "C"

#endif  // Not _WIN32 or __APPLE__.

struct ProfRawHeader {
  size_t binary_ids_size() const {
    if constexpr (INSTR_PROF_RAW_VERSION < 6) {
      return 0;
    } else {
      return BinaryIdsSize;
    }
  }

#define INSTR_PROF_RAW_HEADER(Type, Name, Initializer) Type Name;
#include <profile/InstrProfData.inc>
};

constexpr size_t kAlignAfterBuildId = sizeof(uintptr_t);

constexpr size_t PaddingSize(size_t chunk_size_bytes) {
  return kAlignAfterBuildId - (chunk_size_bytes % kAlignAfterBuildId);
}

constexpr size_t PaddingSize(cpp20::span<const std::byte> chunk) {
  return PaddingSize(chunk.size_bytes());
}

constexpr size_t BinaryIdsSize(cpp20::span<const std::byte> build_id) {
  if (build_id.empty()) {
    return 0;
  }
  return sizeof(uint64_t) + build_id.size_bytes() + PaddingSize(build_id);
}

[[gnu::const]] cpp20::span<const __llvm_profile_data> ProfDataArray() {
  return {
      DataBegin,
      (reinterpret_cast<const std::byte*>(DataEnd) - reinterpret_cast<const std::byte*>(DataBegin) +
       sizeof(__llvm_profile_data) - 1) /
          sizeof(__llvm_profile_data),
  };
}

// This is the .bss data that gets updated live by instrumented code when the
// bias is set to zero.
[[gnu::const]] cpp20::span<uint64_t> ProfCountersData() {
  return cpp20::span<uint64_t>(CountersBegin, CountersEnd - CountersBegin);
}

[[gnu::const]] ProfRawHeader GetHeader(cpp20::span<const std::byte> build_id) {
  // These are used by the INSTR_PROF_RAW_HEADER initializers.
  const uint64_t DataSize = ProfDataArray().size();
  const uint64_t PaddingBytesBeforeCounters = 0;
  const uint64_t CountersSize = ProfCountersData().size();
  const uint64_t PaddingBytesAfterCounters = 0;
  const uint64_t NamesSize = NamesEnd - NamesBegin;
  auto __llvm_profile_get_magic = []() -> uint64_t { return kMagic; };
  auto __llvm_profile_get_version = []() -> uint64_t { return INSTR_PROF_RAW_VERSION_VAR; };
  auto __llvm_write_binary_ids = [build_id](void* ignored) -> uint64_t {
    ZX_DEBUG_ASSERT(ignored == nullptr);
    return BinaryIdsSize(build_id);
  };

  return {
#define INSTR_PROF_RAW_HEADER(Type, Name, Initializer) .Name = Initializer,
#include <profile/InstrProfData.inc>
  };
}

// Don't publish anything if no functions were actually instrumented.
[[gnu::const]] bool NoData() { return ProfCountersData().empty(); }

}  // namespace

void LlvmProfdata::Init(cpp20::span<const std::byte> build_id) {
  build_id_ = build_id;

  if (NoData()) {
    return;
  }

  // The sequence and sizes here should match the PublishLiveData() code.

  const ProfRawHeader header = GetHeader(build_id_);

  counters_offset_ = sizeof(header) + header.binary_ids_size() +
                     (header.DataSize * sizeof(__llvm_profile_data)) +
                     header.PaddingBytesBeforeCounters;
  counters_size_bytes_ = header.CountersSize * sizeof(uint64_t);
  ZX_ASSERT(counters_size_bytes_ == ProfCountersData().size_bytes());

  size_bytes_ = counters_offset_ + counters_size_bytes_ + header.PaddingBytesAfterCounters;

  const size_t PaddingBytesAfterNames = PaddingSize(header.NamesSize);
  size_bytes_ += header.NamesSize + PaddingBytesAfterNames;
}

cpp20::span<std::byte> LlvmProfdata::DoFixedData(cpp20::span<std::byte> data, bool match) {
  if (size_bytes_ == 0) {
    return {};
  }

  // Write bytes at the start of data and then advance data to be the remaining
  // subspan where the next call will write its data.  When merging, this
  // doesn't actually write but instead asserts that the destination already
  // has identical contents.
  auto write_bytes = [&](cpp20::span<const std::byte> bytes, const char* what) {
    ZX_ASSERT_MSG(data.size_bytes() >= bytes.size_bytes(),
                  "%s of %zu bytes with only %zu bytes left!", what, bytes.size_bytes(),
                  data.size_bytes());
    if (match) {
      ZX_ASSERT_MSG(!memcmp(data.data(), bytes.data(), bytes.size()),
                    "mismatch somewhere in %zu bytes of %s", bytes.size(), what);
    } else {
      memcpy(data.data(), bytes.data(), bytes.size());
    }
    data = data.subspan(bytes.size());
  };

  constexpr std::array<std::byte, sizeof(uint64_t)> kPaddingBytes{};
  const cpp20::span kPadding(kPaddingBytes);
  constexpr const char* kPaddingDoc = "alignment padding";

  // These are all the chunks to be written.
  // The sequence and sizes here must match the size_bytes() code.

  const ProfRawHeader header = GetHeader(build_id_);
  write_bytes(cpp20::as_bytes(cpp20::span{&header, 1}), "INSTR_PROF_RAW_HEADER");

  const uint64_t build_id_size = build_id_.size_bytes();
  if (build_id_size > 0) {
    write_bytes(cpp20::as_bytes(cpp20::span{&build_id_size, 1}), "build ID size");
    write_bytes(cpp20::as_bytes(build_id_), "build ID");
    write_bytes(kPadding.subspan(0, PaddingSize(build_id_)), kPaddingDoc);
  }

  auto prof_data = cpp20::span(DataBegin, DataEnd - DataBegin);
  write_bytes(cpp20::as_bytes(prof_data), INSTR_PROF_DATA_SECT_NAME);
  write_bytes(kPadding.subspan(0, header.PaddingBytesBeforeCounters), kPaddingDoc);

  // Skip over the space in the data blob for the counters.
  ZX_ASSERT(counters_size_bytes_ == ProfCountersData().size_bytes());
  ZX_ASSERT_MSG(data.size_bytes() >= counters_size_bytes_,
                "%zu bytes of counters with only %zu bytes left!", counters_size_bytes_,
                data.size_bytes());
  cpp20::span counters_data = data.subspan(0, counters_size_bytes_);
  data = data.subspan(counters_size_bytes_);

  write_bytes(kPadding.subspan(0, header.PaddingBytesAfterCounters), kPaddingDoc);

  auto prof_names = cpp20::span(NamesBegin, NamesEnd - NamesBegin);
  const size_t PaddingBytesAfterNames = PaddingSize(header.NamesSize);
  write_bytes(cpp20::as_bytes(prof_names), INSTR_PROF_NAME_SECT_NAME);
  write_bytes(kPadding.subspan(0, PaddingBytesAfterNames), kPaddingDoc);

  return counters_data;
}

void LlvmProfdata::CopyCounters(cpp20::span<std::byte> data) {
  auto prof_counters = ProfCountersData();
  ZX_ASSERT_MSG(data.size_bytes() >= prof_counters.size_bytes(),
                "writing %zu bytes of counters with only %zu bytes left!", data.size_bytes(),
                data.size_bytes());

  memcpy(data.data(), prof_counters.data(), prof_counters.size_bytes());
}

// Instead of copying, merge the old counters with our values by summation.
void LlvmProfdata::MergeCounters(cpp20::span<std::byte> data) {
  auto prof_counters = ProfCountersData();
  ZX_ASSERT_MSG(data.size_bytes() >= prof_counters.size_bytes(),
                "merging %zu bytes of counters with only %zu bytes left!",
                prof_counters.size_bytes(), data.size_bytes());
  MergeCounters(data.subspan(0, prof_counters.size_bytes()), cpp20::as_bytes(prof_counters));
}

void LlvmProfdata::MergeCounters(cpp20::span<std::byte> to, cpp20::span<const std::byte> from) {
  ZX_ASSERT(to.size_bytes() == from.size_bytes());
  ZX_ASSERT(to.size_bytes() % sizeof(uint64_t) == 0);

  cpp20::span to_counters{reinterpret_cast<uint64_t*>(to.data()),
                          to.size_bytes() / sizeof(uint64_t)};

  cpp20::span from_counters{reinterpret_cast<const uint64_t*>(from.data()),
                            from.size_bytes() / sizeof(uint64_t)};

  for (size_t i = 0; i < to_counters.size(); ++i) {
    to_counters[i] += from_counters[i];
  }
}

void LlvmProfdata::UseCounters(cpp20::span<std::byte> data) {
  auto prof_counters = ProfCountersData();
  ZX_ASSERT_MSG(data.size_bytes() >= prof_counters.size_bytes(),
                "cannot relocate %zu bytes of counters with only %zu bytes left!",
                prof_counters.size_bytes(), data.size_bytes());

  const uintptr_t old_addr = reinterpret_cast<uintptr_t>(prof_counters.data());
  const uintptr_t new_addr = reinterpret_cast<uintptr_t>(data.data());
  ZX_ASSERT(new_addr % kAlign == 0);
  const intptr_t counters_bias = new_addr - old_addr;

  // Now that the data has been copied (or merged), start updating the new
  // copy.  These compiler barriers should ensure we've finished all the
  // copying before updating the bias that the instrumented code uses.
  std::atomic_signal_fence(std::memory_order_seq_cst);
  INSTR_PROF_PROFILE_COUNTER_BIAS_VAR = counters_bias;
  std::atomic_signal_fence(std::memory_order_seq_cst);
}

void LlvmProfdata::UseLinkTimeCounters() {
  std::atomic_signal_fence(std::memory_order_seq_cst);
  INSTR_PROF_PROFILE_COUNTER_BIAS_VAR = 0;
  std::atomic_signal_fence(std::memory_order_seq_cst);
}

cpp20::span<const std::byte> LlvmProfdata::BuildIdFromRawProfile(
    cpp20::span<const std::byte> data) {
  ProfRawHeader header;
  if (data.size() < sizeof(header)) {
    return {};
  }
  memcpy(&header, data.data(), sizeof(header));
  data = data.subspan(sizeof(header));

  if (header.Magic != kMagic || header.Version < 7) {
    return {};
  }

  if (header.binary_ids_size() == 0 || header.binary_ids_size() > data.size()) {
    return {};
  }
  data = data.subspan(0, header.binary_ids_size());

  uint64_t build_id_size;
  if (data.size() < sizeof(build_id_size)) {
    return {};
  }
  memcpy(&build_id_size, data.data(), sizeof(build_id_size));
  data = data.subspan(sizeof(build_id_size));

  if (data.size() < build_id_size) {
    return {};
  }
  return data.subspan(0, build_id_size);
}

bool LlvmProfdata::Match(cpp20::span<const std::byte> data) {
  cpp20::span id = BuildIdFromRawProfile(data);
  return !id.empty() && id.size_bytes() == build_id_.size_bytes() &&
         !memcmp(id.data(), build_id_.data(), build_id_.size_bytes());
}

#endif  // HAVE_PROFDATA
