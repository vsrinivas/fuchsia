// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arch/x86/cpuid.h>

#include <memory>
#include <pow2.h>

namespace cpu_id {
namespace {

template <uint32_t base>
constexpr uint32_t ExtendedLeaf() {
    return 0x80000000 + base;
}

// Extracts the bit range from [lower_bound, upper_bound] (inclusive) from input
// and returns it as a type T.
//TODO(edcoyne): Extract this into a common header for other kernel code.
template <typename T>
inline T ExtractBits(uint32_t input, uint32_t upper_bound, uint32_t lower_bound) {
    // Add one to upper bound because it is inclusive.
    const auto bit_count = upper_bound + 1 - lower_bound;

    DEBUG_ASSERT_MSG(bit_count <= sizeof(T) * 8, "%u <= %lu, upper_bound: %u, lower_bound: %u",
                     bit_count, sizeof(T) * 8, upper_bound, lower_bound);
    return static_cast<T>((input >> lower_bound) & (valpow2(bit_count) - 1));
}

inline uint8_t BaseFamilyFromEax(uint32_t eax) {
    return ExtractBits<uint8_t>(eax, 11, 8);
}

Registers CallCpuId(uint32_t leaf, uint32_t subleaf = 0) {
    Registers result;
    // Set EAX and ECX to the initial values, call cpuid and copy results into
    // the result object.
    asm volatile(
        "cpuid"
        : "=a"(result.reg[Registers::EAX]),
          "=b"(result.reg[Registers::EBX]),
          "=c"(result.reg[Registers::ECX]),
          "=d"(result.reg[Registers::EDX])
        : "a"(leaf), "c"(subleaf));
    return result;
}

// Convert power of two value to a shift_width.
uint8_t ToShiftWidth(uint32_t value) {
    return static_cast<uint8_t>(log2_uint_ceil(value));
}

Registers FindHighestCacheSubleaf(uint32_t leaf) {
    std::optional<Registers> highest;
    for (uint32_t i = 0; i < 1000; i++) {
        const Registers current = CallCpuId(leaf, i);
        if ((current.eax() & 0xF) == 0) { // Null level
            // If we encounter a null level the last level should be the
            // highest.

            // If there is no highest just return the current.
            if (!highest) {
                printf("WARNING: unable to find any cache levels.\n");
                return current;
            }
            return *highest;
        }

        // We want to find the numerically highest cache level.
        if (highest &&
            ((*highest).eax() & 0xFF) < (current.eax() & 0xFF)) {
            highest = {current};
        }
    }

    printf("WARNING: more than 1000 levels of cache, couldn't find highest.\n");
    return *highest;
}

} // namespace

ManufacturerInfo CpuId::ReadManufacturerInfo() const {
    return ManufacturerInfo(CallCpuId(0), CallCpuId(ExtendedLeaf<0>()));
}

ProcessorId CpuId::ReadProcessorId() const {
    return ProcessorId(CallCpuId(1));
}

Features CpuId::ReadFeatures() const {
    return Features(CallCpuId(1), CallCpuId(7), CallCpuId(ExtendedLeaf<1>()));
}

Topology CpuId::ReadTopology() const {
    SubLeaves<Topology::kEaxBSubleaves> leafB = {
        CallCpuId(0xB, 0),
        CallCpuId(0xB, 1),
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

size_t ManufacturerInfo::highest_cpuid_leaf() const {
    return leaf0_.eax();
}

size_t ManufacturerInfo::highest_extended_cpuid_leaf() const {
    return leaf8_0_.eax();
}

ProcessorId::ProcessorId(Registers registers)
    : registers_(registers) {}

uint8_t ProcessorId::stepping() const {
    return registers_.eax() & 0xF;
}

uint16_t ProcessorId::model() const {
    const uint8_t base = ExtractBits<uint8_t>(registers_.eax(), 7, 4);
    const uint8_t extended = ExtractBits<uint8_t>(registers_.eax(), 19, 16);

    const uint8_t family = BaseFamilyFromEax(registers_.eax());
    if (family == 0xF || family == 0x6) {
        return static_cast<uint16_t>((extended << 4) + base);
    } else {
        return base;
    }
}

uint16_t ProcessorId::family() const {
    const uint8_t base = BaseFamilyFromEax(registers_.eax());
    const uint8_t extended = ExtractBits<uint8_t>(registers_.eax(), 27, 20);
    if (base == 0xF) {
        return static_cast<uint16_t>(base + extended);
    } else {
        return base;
    }
}

uint8_t ProcessorId::local_apic_id() const {
    return ExtractBits<uint8_t>(registers_.ebx(), 31, 24);
}

Features::Features(Registers leaf1, Registers leaf7, Registers leaf8_01)
    : leaves_{leaf1, leaf7, leaf8_01} {}

uint8_t Features::max_logical_processors_in_package() const {
    return ExtractBits<uint8_t>(leaves_[LEAF1].ebx(), 23, 16);
}

Topology::Topology(ManufacturerInfo info, Features features, Registers leaf4,
                   SubLeaves<kEaxBSubleaves> leafB, Registers leaf8_8, Registers leaf8_1D,
                   Registers leaf8_1E)
    : info_(info), features_(features), leaf4_(leaf4), leafB_(leafB), leaf8_8_(leaf8_8),
      leaf8_1D_(leaf8_1D), leaf8_1E_(leaf8_1E) {}

std::optional<Topology::Levels> Topology::IntelLevels() const {
    Topology::Levels levels;
    if (info_.highest_cpuid_leaf() >= 11) {
        int nodes_under_previous_level = 0;
        int bits_in_previous_levels = 0;
        for (int i = 0; i < 3; i++) {
            const uint8_t width = ExtractBits<uint8_t>(leafB_.subleaf[i].eax(), 4, 0);
            if (width) {
                uint8_t raw_type = ExtractBits<uint8_t>(leafB_.subleaf[i].ecx(), 15, 8);

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
                const uint16_t nodes_under_level =
                    ExtractBits<uint8_t>(leafB_.subleaf[i].ebx(), 7, 0);
                const uint8_t node_count = static_cast<uint8_t>(
                    nodes_under_level / ((nodes_under_previous_level == 0) ? 1 : nodes_under_previous_level));
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
            const auto cores_in_package = ExtractBits<uint8_t>(leaf4_.eax(), 31, 26) + 1;
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
        printf("WARNING: Unable to parse topology, missing necessary ACPI leaves. "
               "They may be disabled in the bios.\n");
        return std::nullopt;
    }

    return (levels.level_count == 0) ? std::nullopt : std::make_optional(levels);
}

std::optional<Topology::Levels> Topology::AmdLevels() const {
    Topology::Levels levels;
    if (info_.highest_extended_cpuid_leaf() >= ExtendedLeaf<8>()) {
        uint8_t thread_id_bits = ExtractBits<uint8_t>(leaf8_8_.ecx(), 15, 12);
        if (thread_id_bits == 0) {
            thread_id_bits = static_cast<uint8_t>(
                log2_uint_ceil(ExtractBits<uint8_t>(leaf8_8_.ecx(), 7, 0) + 1));
        }

        const uint8_t smt_bits = static_cast<uint8_t>(
            log2_uint_ceil(ExtractBits<uint8_t>(leaf8_1E_.ebx(), 15, 8) + 1));

        const uint8_t node_bits = static_cast<uint8_t>(
            log2_uint_ceil(ExtractBits<uint8_t>(leaf8_1E_.ecx(), 10, 8) + 1));

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
                .type = LevelType::DIE, // NODE in AMD parlance seems to equate to a die.
                .id_bits = node_bits,
            };
        }

    } else {
        const uint8_t core_bits = static_cast<uint8_t>(
            log2_uint_ceil(features_.max_logical_processors_in_package()));
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

    printf("WARNING: Unable to parse topology from CPUID. If this is an Intel chip, "
           "ensure IA32_MISC_ENABLES[22] is off.\n");
    return std::nullopt;
}

Topology::Cache Topology::highest_level_cache() const {
    const auto& leaf = (leaf4_.eax() != 0) ? leaf4_ /*Intel*/ : leaf8_1D_ /*AMD*/;
    const uint16_t threads_sharing_cache =
        static_cast<uint16_t>(ExtractBits<uint16_t>(leaf.eax(), 25, 14) + 1);
    return {
        .level = ExtractBits<uint8_t>(leaf.eax(), 7, 5),
        .shift_width = ToShiftWidth(threads_sharing_cache),
        .size_bytes = (ExtractBits<uint16_t>(leaf.ebx(), 31, 22) + 1) *
                      (ExtractBits<uint16_t>(leaf.ebx(), 21, 12) + 1) *
                      (ExtractBits<uint16_t>(leaf.ebx(), 11, 0) + 1) *
                      (leaf.ecx() + 1),
    };
}

} // namespace cpu_id
