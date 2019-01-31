// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arch/x86/cpuid.h>

#include <memory>

namespace cpu_id {
namespace {

inline uint8_t BaseFamilyFromEax(uint64_t eax) {
    return (eax & 0xF00) >> 8;
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
uint8_t ToShiftWidth(size_t value) {
    if (value == 0) {
        return 0;
    }

    uint8_t count = 0;
    while (!((value >> count++) & 1))
        ;
    return --count; // an extra increment was added before testing.
}

} // namespace

ManufacturerInfo CpuId::ReadManufacturerInfo() const {
    return ManufacturerInfo(CallCpuId(0));
}

ProcessorId CpuId::ReadProcessorId() const {
    return ProcessorId(CallCpuId(1));
}

Features CpuId::ReadFeatures() const {
    return Features(CallCpuId(1), CallCpuId(7), CallCpuId(0x80000001));
}

Topology CpuId::ReadTopology() const {
    Registers leafB[] = {
        CallCpuId(11, 0),
        CallCpuId(11, 1),
        CallCpuId(11, 2)};
    return Topology(ReadManufacturerInfo(), ReadFeatures(), CallCpuId(4), leafB);
}

ManufacturerInfo::ManufacturerInfo(Registers registers)
    : registers_(registers) {}

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
    } translator = {.regs = {registers_.ebx(), registers_.edx(), registers_.ecx()}};

    memcpy(buffer, translator.string, kManufacturerIdLength);
}

size_t ManufacturerInfo::highest_cpuid_leaf() const {
    return registers_.eax();
}

ProcessorId::ProcessorId(Registers registers)
    : registers_(registers) {}

uint8_t ProcessorId::stepping() const {
    return registers_.eax() & 0xF;
}

uint16_t ProcessorId::model() const {
    const uint8_t base = (registers_.eax() >> 4) & 0xF;
    const uint8_t extended = (registers_.eax() >> 16) & 0xF;

    const uint8_t family = BaseFamilyFromEax(registers_.eax());
    if (family == 0xF || family == 0x6) {
        return static_cast<uint16_t>((extended << 4) + base);
    } else {
        return base;
    }
}

uint16_t ProcessorId::family() const {
    const uint8_t base = BaseFamilyFromEax(registers_.eax());
    const uint8_t extended = (registers_.eax() >> 20) & 0xFF;
    if (base == 0xF) {
        return static_cast<uint16_t>(base + extended);
    } else {
        return base;
    }
}

uint8_t ProcessorId::local_apic_id() const {
    return static_cast<uint8_t>((registers_.ebx() >> 24) & 0xFF);
}

Features::Features(Registers leaf1, Registers leaf7, Registers leaf8_01)
    : leaves_{leaf1, leaf7, leaf8_01} {}

uint8_t Features::max_logical_processors_in_package() const {
    return (leaves_[LEAF1].ebx() >> 16) & 0x7F;
}

Topology::Topology(ManufacturerInfo info, Features features, Registers leaf4, Registers* leafB)
    : info_(info), features_(features), leaf4_(leaf4), leafB_{leafB[0], leafB[1], leafB[2]} {}

std::optional<Topology::Levels> Topology::IntelLevels() const {
    Topology::Levels levels;
    if (info_.highest_cpuid_leaf() >= 11) {
        int nodes_under_previous_level = 0;
        int bits_in_previous_levels = 0;
        for (int i = 0; i < 3; i++) {
            const uint8_t width = leafB_[i].eax() & 0xF;
            if (width) {
                uint8_t raw_type = (leafB_[i].ecx() & 0xFF00) >> 8;

                LevelType type = LevelType::INVALID;
                if (raw_type == 1) {
                    type = LevelType::SMT;
                } else if (raw_type == 2) {
                    type = LevelType::CORE;
                } else if (i == 2) {
                    // Package is defined as the "last" level.
                    type = LevelType::PACKAGE;
                }

                // This actually contains total logical processors in all
                // subtrees of this level of the topology.
                const uint16_t nodes_under_level = leafB_[i].ebx() & 0xFF;
                const uint8_t node_count = static_cast<uint8_t>(
                    nodes_under_level / ((nodes_under_previous_level == 0) ? 1 : nodes_under_previous_level));
                const uint8_t shift_width = static_cast<uint8_t>(width - bits_in_previous_levels);

                levels.levels[levels.level_count++] = {
                    .type = type,
                    // Dividing by logical processors under last level will give
                    // us the nodes that are at this level.
                    .node_count = node_count,
                    .shift_width = shift_width,
                };
                nodes_under_previous_level += nodes_under_level;
                bits_in_previous_levels += shift_width;
            }
        }
    } else if (info_.highest_cpuid_leaf() >= 4) {
        const bool single_core = !features_.HasFeature(Features::HTT);
        if (single_core) {
            levels.levels[levels.level_count++] = {
                .type = LevelType::PACKAGE,
                .node_count = 1,
                .shift_width = 0,
            };
        } else {
            const auto logical_in_package = features_.max_logical_processors_in_package();
            const auto cores_in_package = ((leaf4_.eax() >> 26) & 0x3F) + 1;
            const auto logical_per_core = logical_in_package / cores_in_package;
            if (logical_per_core > 1) {
                levels.levels[levels.level_count++] = {
                    .type = LevelType::SMT,
                    .shift_width = ToShiftWidth(logical_per_core),
                };
            }

            if (cores_in_package > 1) {
                levels.levels[levels.level_count++] = {
                    .type = LevelType::CORE,
                    .shift_width = ToShiftWidth(cores_in_package),
                };
            }
        }
    } else {
        // If this is an intel CPU then cpuid leaves are disabled on the system
        // IA32_MISC_ENABLES[22] == 1. This can be set to 0, usually in the BIOS,
        // the kernel can change it too but we prefer to stay read-only here.
        return std::nullopt;
    }

    return {levels};
}

std::optional<Topology::Levels> Topology::levels() const {
    auto levels = IntelLevels();
    if (levels) {
        return levels;
    }

    // If Intel approach didn't work try the AMD approach, even on AMD chips the
    // intel approach may work, there are hypervisor cases that populate it.
    // TODO(edcoyne): Implement AMD approach.
    printf("WARNING: AMD processors are not yet supported. \n");

    printf("WARNING: Unable to parse topology from CPUID. If this is an Intel chip, "
           "ensure IA32_MISC_ENABLES[22] is off.\n");
    return std::nullopt;
}

} // namespace cpu_id
