// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "arch/x86/cpuid.h"

#include <pow2.h>
#include <trace.h>

#include <memory>

#include <fbl/bits.h>

#define LOCAL_TRACE 0

namespace cpu_id {
namespace {

template <uint32_t base>
constexpr uint32_t ExtendedLeaf() {
  return 0x80000000 + base;
}

inline uint8_t BaseFamilyFromEax(uint32_t eax) { return fbl::ExtractBits<11, 8, uint8_t>(eax); }

Registers CallCpuId(uint32_t leaf, uint32_t subleaf = 0) {
  Registers result;
  // Set EAX and ECX to the initial values, call cpuid and copy results into
  // the result object.
  asm volatile("cpuid"
               : "=a"(result.reg[Registers::EAX]), "=b"(result.reg[Registers::EBX]),
                 "=c"(result.reg[Registers::ECX]), "=d"(result.reg[Registers::EDX])
               : "a"(leaf), "c"(subleaf));
  return result;
}

}  // namespace

ManufacturerInfo CpuId::ReadManufacturerInfo() const {
  return ManufacturerInfo(CallCpuId(0), CallCpuId(ExtendedLeaf<0>()));
}

ProcessorId CpuId::ReadProcessorId() const { return ProcessorId(CallCpuId(1)); }

Features CpuId::ReadFeatures() const {
  return Features(CallCpuId(1), CallCpuId(6), CallCpuId(7), CallCpuId(ExtendedLeaf<1>()),
                  CallCpuId(ExtendedLeaf<7>()));
}

ManufacturerInfo::ManufacturerInfo(Registers leaf0, Registers leaf8_0)
    : leaf0_(leaf0), leaf8_0_(leaf8_0) {}

ManufacturerInfo::Manufacturer ManufacturerInfo::manufacturer() const {
  char buffer[kManufacturerIdLength + 1] = {0};
  manufacturer_id(buffer);
  if (strcmp("GenuineIntel", buffer) == 0) {
    return INTEL;
  } else if (strcmp("AuthenticAMD", buffer) == 0) {
    return AMD;
  } else {
    return OTHER;
  }
}

void ManufacturerInfo::manufacturer_id(char* buffer) const {
  union {
    uint32_t regs[3];
    char string[13];
  } translator = {.regs = {leaf0_.ebx(), leaf0_.edx(), leaf0_.ecx()}};

  memcpy(buffer, translator.string, kManufacturerIdLength);
}

size_t ManufacturerInfo::highest_cpuid_leaf() const { return leaf0_.eax(); }

size_t ManufacturerInfo::highest_extended_cpuid_leaf() const { return leaf8_0_.eax(); }

ProcessorId::ProcessorId(Registers registers) : registers_(registers) {}

uint8_t ProcessorId::stepping() const { return registers_.eax() & 0xF; }

uint16_t ProcessorId::model() const {
  const uint8_t base = fbl::ExtractBits<7, 4, uint8_t>(registers_.eax());
  const uint8_t extended = fbl::ExtractBits<19, 16, uint8_t>(registers_.eax());

  const uint8_t family = BaseFamilyFromEax(registers_.eax());
  if (family == 0xF || family == 0x6) {
    return static_cast<uint16_t>((extended << 4) + base);
  } else {
    return base;
  }
}

uint16_t ProcessorId::family() const {
  const uint8_t base = BaseFamilyFromEax(registers_.eax());
  const uint8_t extended = fbl::ExtractBits<27, 20, uint8_t>(registers_.eax());
  if (base == 0xF) {
    return static_cast<uint16_t>(base + extended);
  } else {
    return base;
  }
}

uint32_t ProcessorId::signature() const { return registers_.eax(); }

uint8_t ProcessorId::local_apic_id() const {
  return fbl::ExtractBits<31, 24, uint8_t>(registers_.ebx());
}

Features::Features(Registers leaf1, Registers leaf6, Registers leaf7, Registers leaf8_01,
                   Registers leaf8_07)
    : leaves_{leaf1, leaf6, leaf7, leaf8_01, leaf8_07} {}

uint8_t Features::max_logical_processors_in_package() const {
  return fbl::ExtractBits<23, 16, uint8_t>(leaves_[LEAF1].ebx());
}

}  // namespace cpu_id
