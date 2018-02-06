// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/feature.h>

#include <arch/arm64.h>
#include <bits.h>
#include <fbl/algorithm.h>
#include <inttypes.h>

// saved feature bitmap
uint32_t arm64_features;

static arm64_cache_info_t cache_info[SMP_MAX_CPUS];

// cache size parameters cpus, default to a reasonable minimum
uint32_t arm64_zva_size = 32;
uint32_t arm64_icache_size = 32;
uint32_t arm64_dcache_size = 32;

static void parse_ccsid(arm64_cache_desc_t* desc, uint64_t ccsid) {
    desc->write_through = BIT(ccsid, 31) > 0;
    desc->write_back = BIT(ccsid, 30) > 0;
    desc->read_alloc = BIT(ccsid, 29) > 0;
    desc->write_alloc = BIT(ccsid, 28) > 0;
    desc->num_sets = (uint32_t)BITS_SHIFT(ccsid, 27, 13) + 1;
    desc->associativity = (uint32_t)BITS_SHIFT(ccsid, 12, 3) + 1;
    desc->line_size = 1u << (BITS(ccsid, 2, 0) + 4);
}

void arm64_get_cache_info(arm64_cache_info_t* info) {
    uint64_t temp = 0;

    uint64_t sysreg = ARM64_READ_SYSREG(clidr_el1);
    info->inner_boundary = (uint8_t)BITS_SHIFT(sysreg, 32, 30);
    info->lou_u = (uint8_t)BITS_SHIFT(sysreg, 29, 27);
    info->loc = (uint8_t)BITS_SHIFT(sysreg, 26, 24);
    info->lou_is = (uint8_t)BITS_SHIFT(sysreg, 23, 21);
    for (int i = 0; i < 7; i++) {
        uint8_t ctype = (sysreg >> (3 * i)) & 0x07;
        if (ctype == 0) {
            info->level_data_type[i].ctype = 0;
            info->level_inst_type[i].ctype = 0;
        } else if (ctype == 4) {                               // Unified
            ARM64_WRITE_SYSREG(CSSELR_EL1, (int64_t)(i << 1)); // Select cache level
            temp = ARM64_READ_SYSREG(ccsidr_el1);
            info->level_data_type[i].ctype = 4;
            parse_ccsid(&(info->level_data_type[i]), temp);
        } else {
            if (ctype & 0x02) {
                ARM64_WRITE_SYSREG(CSSELR_EL1, (int64_t)(i << 1));
                temp = ARM64_READ_SYSREG(ccsidr_el1);
                info->level_data_type[i].ctype = 2;
                parse_ccsid(&(info->level_data_type[i]), temp);
            }
            if (ctype & 0x01) {
                ARM64_WRITE_SYSREG(CSSELR_EL1, (int64_t)(i << 1) | 0x01);
                temp = ARM64_READ_SYSREG(ccsidr_el1);
                info->level_inst_type[i].ctype = 1;
                parse_ccsid(&(info->level_inst_type[i]), temp);
            }
        }
    }
}

void arm64_dump_cache_info(uint32_t cpu) {

    arm64_cache_info_t* info = &(cache_info[cpu]);
    printf("==== ARM64 CACHE INFO CORE %u ====\n", cpu);
    printf("Inner Boundary = L%u\n", info->inner_boundary);
    printf("Level of Unification Uniprocessor = L%u\n", info->lou_u);
    printf("Level of Coherence = L%u\n", info->loc);
    printf("Level of Unification Inner Shareable = L%u\n", info->lou_is);
    for (int i = 0; i < 7; i++) {
        printf("L%d Details:", i + 1);
        if ((info->level_data_type[i].ctype == 0) && (info->level_inst_type[i].ctype == 0)) {
            printf("\tNot Implemented\n");
        } else {
            if (info->level_data_type[i].ctype == 4) {
                printf("\tUnified Cache, sets=%u, associativity=%u, line size=%u bytes\n",
                       info->level_data_type[i].num_sets,
                       info->level_data_type[i].associativity,
                       info->level_data_type[i].line_size);
            } else {
                if (info->level_data_type[i].ctype & 0x02) {
                    printf("\tData Cache, sets=%u, associativity=%u, line size=%u bytes\n",
                           info->level_data_type[i].num_sets,
                           info->level_data_type[i].associativity,
                           info->level_data_type[i].line_size);
                }
                if (info->level_inst_type[i].ctype & 0x01) {
                    if (info->level_data_type[i].ctype & 0x02) {
                        printf("\t");
                    }
                    printf("\tInstruction Cache, sets=%u, associativity=%u, line size=%u bytes\n",
                           info->level_inst_type[i].num_sets,
                           info->level_inst_type[i].associativity,
                           info->level_inst_type[i].line_size);
                }
            }
        }
    }
}

static void midr_to_core(uint32_t midr, char* str, size_t len) {
    __UNUSED uint32_t implementer = BITS_SHIFT(midr, 31, 24);
    __UNUSED uint32_t variant = BITS_SHIFT(midr, 23, 20);
    __UNUSED uint32_t architecture = BITS_SHIFT(midr, 19, 16);
    __UNUSED uint32_t partnum = BITS_SHIFT(midr, 15, 4);
    __UNUSED uint32_t revision = BITS_SHIFT(midr, 3, 0);

    const char* partnum_str = "unknown";
    if (implementer == 'A') {
        // ARM cores
        switch (partnum) {
        case 0xd03:
            partnum_str = "ARM Cortex-a53";
            break;
        case 0xd04:
            partnum_str = "ARM Cortex-a35";
            break;
        case 0xd07:
            partnum_str = "ARM Cortex-a57";
            break;
        case 0xd08:
            partnum_str = "ARM Cortex-a72";
            break;
        case 0xd09:
            partnum_str = "ARM Cortex-a73";
            break;
        }
    } else if (implementer == 'C' && partnum == 0xa1) {
        // Cavium
        partnum_str = "Cavium CN88XX";
    }

    snprintf(str, len, "%s r%up%u", partnum_str, variant, revision);
}

static void print_cpu_info() {
    uint32_t midr = (uint32_t)ARM64_READ_SYSREG(midr_el1);
    char cpu_name[128];
    midr_to_core(midr, cpu_name, sizeof(cpu_name));

    uint64_t mpidr = ARM64_READ_SYSREG(mpidr_el1);

    dprintf(INFO, "ARM cpu %u: midr %#x '%s' mpidr %#" PRIx64 " aff %u:%u:%u:%u\n",
            arch_curr_cpu_num(), midr, cpu_name, mpidr,
            (uint32_t)((mpidr & MPIDR_AFF3_MASK) >> MPIDR_AFF3_SHIFT),
            (uint32_t)((mpidr & MPIDR_AFF2_MASK) >> MPIDR_AFF2_SHIFT),
            (uint32_t)((mpidr & MPIDR_AFF1_MASK) >> MPIDR_AFF1_SHIFT),
            (uint32_t)((mpidr & MPIDR_AFF0_MASK) >> MPIDR_AFF0_SHIFT));
}

// call on every cpu to save features
void arm64_feature_init() {
    // set up some global constants based on the boot cpu
    cpu_num_t cpu = arch_curr_cpu_num();
    if (cpu == 0) {
        // read the block size of DC ZVA
        uint64_t dczid = ARM64_READ_SYSREG(dczid_el0);
        uint32_t arm64_zva_shift = 0;
        if (BIT(dczid, 4) == 0) {
            arm64_zva_shift = (uint32_t)(ARM64_READ_SYSREG(dczid_el0) & 0xf) + 2;
        }
        ASSERT(arm64_zva_shift != 0); // for now, fail if DC ZVA is unavailable
        arm64_zva_size = (1u << arm64_zva_shift);

        // read the dcache and icache line size
        uint64_t ctr = ARM64_READ_SYSREG(ctr_el0);
        uint32_t arm64_dcache_shift = (uint32_t)BITS_SHIFT(ctr, 19, 16) + 2;
        arm64_dcache_size = (1u << arm64_dcache_shift);
        uint32_t arm64_icache_shift = (uint32_t)BITS(ctr, 3, 0) + 2;
        arm64_icache_size = (1u << arm64_icache_shift);

        // parse the ISA feature bits
        arm64_features |= ZX_HAS_CPU_FEATURES;
        uint64_t isar0 = ARM64_READ_SYSREG(id_aa64isar0_el1);
        if (BITS_SHIFT(isar0, 7, 4) >= 1) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_AES;
        }
        if (BITS_SHIFT(isar0, 7, 4) >= 2) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_PMULL;
        }
        if (BITS_SHIFT(isar0, 11, 8) >= 1) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_SHA1;
        }
        if (BITS_SHIFT(isar0, 15, 12) >= 1) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_SHA2;
        }
        if (BITS_SHIFT(isar0, 19, 16) >= 1) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_CRC32;
        }
        if (BITS_SHIFT(isar0, 23, 20) >= 1) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_ATOMICS;
        }
        if (BITS_SHIFT(isar0, 31, 28) >= 1) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_RDM;
        }
        if (BITS_SHIFT(isar0, 35, 32) >= 1) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_SHA3;
        }
        if (BITS_SHIFT(isar0, 39, 36) >= 1) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_SM3;
        }
        if (BITS_SHIFT(isar0, 43, 40) >= 1) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_SM4;
        }
        if (BITS_SHIFT(isar0, 47, 44) >= 1) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_DP;
        }

        uint64_t isar1 = ARM64_READ_SYSREG(id_aa64isar1_el1);
        if (BITS_SHIFT(isar1, 3, 0) >= 1) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_DPB;
        }

        uint64_t pfr0 = ARM64_READ_SYSREG(id_aa64pfr0_el1);
        if (BITS_SHIFT(pfr0, 19, 16) < 0b1111) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_FP;
        }
        if (BITS_SHIFT(pfr0, 23, 20) < 0b1111) {
            arm64_features |= ZX_ARM64_FEATURE_ISA_ASIMD;
        }
    }

    // read the cache info for each cpu
    arm64_get_cache_info(&(cache_info[cpu]));

    // check to make sure implementation supports 16 bit asids
    uint64_t mmfr0 = ARM64_READ_SYSREG(ID_AA64MMFR0_EL1);
    ASSERT((mmfr0 & ARM64_MMFR0_ASIDBITS_MASK) == ARM64_MMFR0_ASIDBITS_16);
}

static void print_feature() {
    const struct {
        uint32_t bit;
        const char* name;
    } features[] = {
        {ZX_ARM64_FEATURE_ISA_FP, "fp"},
        {ZX_ARM64_FEATURE_ISA_ASIMD, "asimd"},
        {ZX_ARM64_FEATURE_ISA_AES, "aes"},
        {ZX_ARM64_FEATURE_ISA_PMULL, "pmull"},
        {ZX_ARM64_FEATURE_ISA_SHA1, "sha1"},
        {ZX_ARM64_FEATURE_ISA_SHA2, "sha2"},
        {ZX_ARM64_FEATURE_ISA_CRC32, "crc32"},
        {ZX_ARM64_FEATURE_ISA_ATOMICS, "atomics"},
        {ZX_ARM64_FEATURE_ISA_RDM, "rdm"},
        {ZX_ARM64_FEATURE_ISA_SHA3, "sha3"},
        {ZX_ARM64_FEATURE_ISA_SM3, "sm3"},
        {ZX_ARM64_FEATURE_ISA_SM4, "sm4"},
        {ZX_ARM64_FEATURE_ISA_DP, "dp"},
        {ZX_ARM64_FEATURE_ISA_DPB, "dpb"},
    };

    printf("ARM Features: ");
    uint col = 0;
    for (uint i = 0; i < fbl::count_of(features); ++i) {
        if (arm64_feature_test(features[i].bit))
            col += printf("%s ", features[i].name);
        if (col >= 80) {
            printf("\n");
            col = 0;
        }
    }
    if (col > 0)
        printf("\n");
}

// dump the feature set
// print additional information if full is passed
void arm64_feature_debug(bool full) {
    print_cpu_info();

    if (full) {
        print_feature();
        dprintf(INFO, "ARM cache line sizes: icache %u dcache %u zva %u\n",
                arm64_icache_size, arm64_dcache_size, arm64_zva_size);
        if (LK_DEBUGLEVEL > 0) {
            arm64_dump_cache_info(arch_curr_cpu_num());
        }
    }
}
