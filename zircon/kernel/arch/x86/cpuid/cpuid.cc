// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arch/x86/cpuid.h"

#include <bits.h>
#include <pow2.h>
#include <trace.h>

#include <memory>

#define LOCAL_TRACE 0

namespace cpu_id {
namespace {

template <uint32_t base>
constexpr uint32_t ExtendedLeaf() {
  return 0x80000000 + base;
}

inline uint8_t BaseFamilyFromEax(uint32_t eax) { return ExtractBits<11, 8, uint8_t>(eax); }

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

// Convert power of two value to a shift_width.
uint8_t ToShiftWidth(uint32_t value) { return static_cast<uint8_t>(log2_uint_ceil(value)); }

Registers FindHighestCacheSubleaf(uint32_t leaf) {
  Registers empty = {};

  // Check to see if these are valid leaves for this processor.
  Registers max_leaf = CallCpuId((leaf < ExtendedLeaf<0>()) ? 0 : ExtendedLeaf<0>());
  if (leaf > max_leaf.eax()) {
    // Out of range, return an empty value.
    return empty;
  }

  std::optional<Registers> highest;
  const uint32_t max_cache_levels = 32;
  for (uint32_t i = 0; i < max_cache_levels; i++) {
    const Registers current = CallCpuId(leaf, i);
    LTRACEF("leaf %#x %#x: %#x %#x %#x %#x\n", leaf, i, current.eax(), current.ebx(), current.ecx(),
            current.edx());
    if ((current.eax() & 0xF) == 0) {  // Null level
      // If we encounter a null level the last level should be the highest.

      // If there is no highest just return an empty value.
      if (!highest) {
        printf("WARNING: unable to find any cache levels on leaf %#x.\n", leaf);
        return empty;
      }
      return *highest;
    }

    // We want to find the numerically highest cache level.
    if (!highest || ((*highest).eax() & 0xFF) < (current.eax() & 0xFF)) {
      highest = {current};
    }
  }

  printf("WARNING: more than %u levels of cache, couldn't find highest on leaf %#x\n",
         max_cache_levels, leaf);

  return highest.value_or(empty);
}

}  // namespace

ManufacturerInfo CpuId::ReadManufacturerInfo() const {
  return ManufacturerInfo(CallCpuId(0), CallCpuId(ExtendedLeaf<0>()));
}

ProcessorId CpuId::ReadProcessorId() const { return ProcessorId(CallCpuId(1)); }

Features CpuId::ReadFeatures() const {
  return Features(CallCpuId(1), CallCpuId(6), CallCpuId(7), CallCpuId(ExtendedLeaf<1>()),
                  CallCpuId(ExtendedLeaf<8>()));
}

Topology CpuId::ReadTopology() const {
  SubLeaves<Topology::kEaxBSubleaves> leafB = {CallCpuId(0xB, 0), CallCpuId(0xB, 1),
                                               CallCpuId(0xB, 2)};
  return Topology(ReadManufacturerInfo(), ReadFeatures(), FindHighestCacheSubleaf(4), leafB,
                  CallCpuId(ExtendedLeaf<8>()), FindHighestCacheSubleaf(ExtendedLeaf<0x1D>()),
                  CallCpuId(ExtendedLeaf<0x1E>()));
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
  const uint8_t base = ExtractBits<7, 4, uint8_t>(registers_.eax());
  const uint8_t extended = ExtractBits<19, 16, uint8_t>(registers_.eax());

  const uint8_t family = BaseFamilyFromEax(registers_.eax());
  if (family == 0xF || family == 0x6) {
    return static_cast<uint16_t>((extended << 4) + base);
  } else {
    return base;
  }
}

uint16_t ProcessorId::family() const {
  const uint8_t base = BaseFamilyFromEax(registers_.eax());
  const uint8_t extended = ExtractBits<27, 20, uint8_t>(registers_.eax());
  if (base == 0xF) {
    return static_cast<uint16_t>(base + extended);
  } else {
    return base;
  }
}

uint32_t ProcessorId::signature() const { return registers_.eax(); }

uint8_t ProcessorId::local_apic_id() const {
  return ExtractBits<31, 24, uint8_t>(registers_.ebx());
}

Features::Features(Registers leaf1, Registers leaf6, Registers leaf7, Registers leaf8_01,
                   Registers leaf8_08)
    : leaves_{leaf1, leaf6, leaf7, leaf8_01, leaf8_08} {}

uint8_t Features::max_logical_processors_in_package() const {
  return ExtractBits<23, 16, uint8_t>(leaves_[LEAF1].ebx());
}

Topology::Topology(ManufacturerInfo info, Features features, Registers leaf4,
                   SubLeaves<kEaxBSubleaves> leafB, Registers leaf8_8, Registers leaf8_1D,
                   Registers leaf8_1E)
    : info_(info),
      features_(features),
      leaf4_(leaf4),
      leafB_(leafB),
      leaf8_8_(leaf8_8),
      leaf8_1D_(leaf8_1D),
      leaf8_1E_(leaf8_1E) {}

std::optional<Topology::Levels> Topology::IntelLevels() const {
  Topology::Levels levels;
  if (info_.highest_cpuid_leaf() >= 11) {
    int nodes_under_previous_level = 0;
    int bits_in_previous_levels = 0;
    for (int i = 0; i < 3; i++) {
      const uint8_t width = ExtractBits<4, 0, uint8_t>(leafB_.subleaf[i].eax());
      if (width) {
        uint8_t raw_type = ExtractBits<15, 8, uint8_t>(leafB_.subleaf[i].ecx());

        LevelType type = LevelType::INVALID;
        if (raw_type == 1) {
          type = LevelType::SMT;
        } else if (raw_type == 2) {
          type = LevelType::CORE;
        } else if (i == 2) {
          // Package is defined as the "last" level.
          type = LevelType::DIE;
        }

        // This actually contains total logical processors in all
        // subtrees of this level of the topology.
        const uint16_t nodes_under_level = ExtractBits<7, 0, uint8_t>(leafB_.subleaf[i].ebx());
        const uint8_t node_count = static_cast<uint8_t>(
            nodes_under_level /
            ((nodes_under_previous_level == 0) ? 1 : nodes_under_previous_level));
        const uint8_t id_bits = static_cast<uint8_t>(width - bits_in_previous_levels);

        levels.levels[levels.level_count++] = {
            .type = type,
            // Dividing by logical processors under last level will give
            // us the nodes that are at this level.
            .node_count = node_count,
            .id_bits = id_bits,
        };
        nodes_under_previous_level += nodes_under_level;
        bits_in_previous_levels += id_bits;
      }
    }
  } else if (info_.highest_cpuid_leaf() >= 4) {
    const bool single_core = !features_.HasFeature(Features::HTT);
    if (single_core) {
      levels.levels[levels.level_count++] = {
          .type = LevelType::DIE,
          .node_count = 1,
          .id_bits = 0,
      };
    } else {
      const auto logical_in_package = features_.max_logical_processors_in_package();
      const auto cores_in_package = ExtractBits<31, 26, uint8_t>(leaf4_.eax()) + 1;
      const auto logical_per_core = logical_in_package / cores_in_package;
      if (logical_per_core > 1) {
        levels.levels[levels.level_count++] = {
            .type = LevelType::SMT,
            .id_bits = ToShiftWidth(logical_per_core),
        };
      }

      if (cores_in_package > 1) {
        levels.levels[levels.level_count++] = {
            .type = LevelType::CORE,
            .id_bits = ToShiftWidth(cores_in_package),
        };
      }
    }
  } else {
    // If this is an intel CPU then cpuid leaves are disabled on the system
    // IA32_MISC_ENABLES[22] == 1. This can be set to 0, usually in the BIOS,
    // the kernel can change it too but we prefer to stay read-only here.
    printf(
        "WARNING: Unable to parse topology, missing necessary ACPI leaves. "
        "They may be disabled in the bios.\n");
    return std::nullopt;
  }

  return (levels.level_count == 0) ? std::nullopt : std::make_optional(levels);
}

std::optional<Topology::Levels> Topology::AmdLevels() const {
  Topology::Levels levels;
  if (info_.highest_extended_cpuid_leaf() >= ExtendedLeaf<8>()) {
    uint8_t thread_id_bits = ExtractBits<15, 12, uint8_t>(leaf8_8_.ecx());
    if (thread_id_bits == 0) {
      thread_id_bits =
          static_cast<uint8_t>(log2_uint_ceil(ExtractBits<7, 0, uint8_t>(leaf8_8_.ecx()) + 1));
    }

    const uint8_t smt_bits =
        static_cast<uint8_t>(log2_uint_ceil(ExtractBits<15, 8, uint8_t>(leaf8_1E_.ebx()) + 1));

    const uint8_t node_bits =
        static_cast<uint8_t>(log2_uint_ceil(ExtractBits<10, 8, uint8_t>(leaf8_1E_.ecx()) + 1));

    // thread_id is the unique id of a thread in a package (socket), if we
    // remove the bits used to identify the thread inside of the core (smt)
    // and the bits used to idenfity to which node the core belongs, what is
    // left should be the bits used to id the core?
    const uint8_t core_bits = static_cast<uint8_t>(thread_id_bits - smt_bits - node_bits);

    if (smt_bits) {
      levels.levels[levels.level_count++] = {
          .type = LevelType::SMT,
          .id_bits = smt_bits,
      };
    }

    if (core_bits) {
      levels.levels[levels.level_count++] = {
          .type = LevelType::CORE,
          .id_bits = core_bits,
      };
    }

    if (node_bits) {
      levels.levels[levels.level_count++] = {
          .type = LevelType::DIE,  // NODE in AMD parlance seems to equate to a die.
          .id_bits = node_bits,
      };
    }

  } else {
    const uint8_t core_bits =
        static_cast<uint8_t>(log2_uint_ceil(features_.max_logical_processors_in_package()));
    levels.levels[levels.level_count++] = {
        .type = LevelType::CORE,
        .id_bits = ToShiftWidth(core_bits),
    };
  }

  return (levels.level_count == 0) ? std::nullopt : std::make_optional(levels);
}

std::optional<Topology::Levels> Topology::levels() const {
  auto levels = IntelLevels();
  if (levels) {
    return levels;
  }

  // If Intel approach didn't work try the AMD approach, even on AMD chips the
  // intel approach may work, there are hypervisor cases that populate it.
  levels = AmdLevels();
  if (levels) {
    return levels;
  }

  printf(
      "WARNING: Unable to parse topology from CPUID. If this is an Intel chip, "
      "ensure IA32_MISC_ENABLES[22] is off.\n");
  return std::nullopt;
}

Topology::Cache Topology::highest_level_cache() const {
  const auto& leaf = (leaf4_.eax() != 0) ? leaf4_ /*Intel*/ : leaf8_1D_ /*AMD*/;
  const uint16_t threads_sharing_cache =
      static_cast<uint16_t>(ExtractBits<25, 14, uint16_t>(leaf.eax()) + 1);
  LTRACEF("threads sharing cache %u\n", threads_sharing_cache);
  return {
      .level = ExtractBits<7, 5, uint8_t>(leaf.eax()),
      .shift_width = ToShiftWidth(threads_sharing_cache),
      .size_bytes = (ExtractBits<31, 22, uint16_t>(leaf.ebx()) + 1) *
                    (ExtractBits<21, 12, uint16_t>(leaf.ebx()) + 1) *
                    (ExtractBits<11, 0, uint16_t>(leaf.ebx()) + 1) * (leaf.ecx() + 1),
  };
}

}  // namespace cpu_id
