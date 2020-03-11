// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <cstddef>
#include <cstdio>

#include <profile/InstrProfData.inc>

namespace runtests {

namespace {

using IntPtrT = intptr_t;

enum ValueKind {
#define VALUE_PROF_KIND(Enumerator, Value, Descr) Enumerator = (Value),
#include <profile/InstrProfData.inc>
};

struct __llvm_profile_data {
#define INSTR_PROF_DATA(Type, LLVMType, Name, Initializer) Type Name;
#include <profile/InstrProfData.inc>
};

struct __llvm_profile_header {
#define INSTR_PROF_RAW_HEADER(Type, Name, Initializer) Type Name;
#include <profile/InstrProfData.inc>
};

}  // namespace

bool ProfilesCompatible(const uint8_t* src, uint8_t* dst, size_t size) {
  const __llvm_profile_header* src_header = reinterpret_cast<const __llvm_profile_header*>(src);
  const __llvm_profile_header* dst_header = reinterpret_cast<const __llvm_profile_header*>(dst);

  if (src_header->Magic != dst_header->Magic || src_header->Version != dst_header->Version ||
      src_header->DataSize != dst_header->DataSize ||
      src_header->CountersSize != dst_header->CountersSize ||
      src_header->NamesSize != dst_header->NamesSize)
    return false;

  const __llvm_profile_data* src_data_start =
      reinterpret_cast<const __llvm_profile_data*>(src + sizeof(*src_header));
  const __llvm_profile_data* src_data_end = src_data_start + src_header->DataSize;
  const __llvm_profile_data* dst_data_start =
      reinterpret_cast<__llvm_profile_data*>(dst + sizeof(*dst_header));
  const __llvm_profile_data* dst_data_end = dst_data_start + dst_header->DataSize;

  for (const __llvm_profile_data *src_data = src_data_start, *dst_data = dst_data_start;
       src_data < src_data_end && dst_data < dst_data_end; ++src_data, ++dst_data) {
    if (src_data->NameRef != dst_data->NameRef || src_data->FuncHash != dst_data->FuncHash ||
        src_data->NumCounters != dst_data->NumCounters)
      return false;
  }

  return true;
}

uint8_t* MergeProfiles(const uint8_t* src, uint8_t* dst, size_t size) {
  const __llvm_profile_header* src_header = reinterpret_cast<const __llvm_profile_header*>(src);
  const __llvm_profile_data* src_data_start =
      reinterpret_cast<const __llvm_profile_data*>(src + sizeof(*src_header));
  const __llvm_profile_data* src_data_end = src_data_start + src_header->DataSize;
  const uint64_t* src_counters_start = reinterpret_cast<const uint64_t*>(src_data_end);

  __llvm_profile_header* dst_header = reinterpret_cast<__llvm_profile_header*>(dst);
  __llvm_profile_data* dst_data_start =
      reinterpret_cast<__llvm_profile_data*>(dst + sizeof(*dst_header));
  __llvm_profile_data* dst_data_end = dst_data_start + dst_header->DataSize;
  uint64_t* dst_counters_start = reinterpret_cast<uint64_t*>(dst_data_end);

  const __llvm_profile_data* src_data = src_data_start;
  __llvm_profile_data* dst_data = dst_data_start;
  for (; src_data < src_data_end && dst_data < dst_data_end; src_data++, dst_data++) {
    const uint64_t* src_counters =
        src_counters_start + (src_data->CounterPtr - src_header->CountersDelta) / sizeof(uint64_t);
    uint64_t* dst_counters =
        dst_counters_start + (dst_data->CounterPtr - dst_header->CountersDelta) / sizeof(uint64_t);
    for (unsigned i = 0; i < src_data->NumCounters; i++) {
      dst_counters[i] += src_counters[i];
    }
  }

  return dst;
}

}  // namespace runtests
