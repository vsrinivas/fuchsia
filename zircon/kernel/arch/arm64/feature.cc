// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "arch/arm64/feature.h"

#include <bits.h>
#include <inttypes.h>
#include <lib/arch/arm64/feature.h>
#include <lib/arch/intrin.h>

#include <arch/arm64.h>
#include <fbl/algorithm.h>
#include <kernel/cpu.h>
#include <ktl/iterator.h>

// saved feature bitmap
uint32_t arm64_isa_features;

static arm64_cache_info_t cache_info[SMP_MAX_CPUS];

enum arm64_asid_width arm64_asid_width_;

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

  uint64_t clidr = __arm_rsr64("clidr_el1");
  info->inner_boundary = (uint8_t)BITS_SHIFT(clidr, 32, 30);
  info->lou_u = (uint8_t)BITS_SHIFT(clidr, 29, 27);
  info->loc = (uint8_t)BITS_SHIFT(clidr, 26, 24);
  info->lou_is = (uint8_t)BITS_SHIFT(clidr, 23, 21);

  uint64_t ctr = __arm_rsr64("ctr_el0");
  info->imin_line = (uint8_t)BITS(ctr, 3, 0);
  info->dmin_line = (uint8_t)BITS_SHIFT(ctr, 19, 16);
  info->l1_instruction_cache_policy = (uint8_t)BITS_SHIFT(ctr, 15, 14);
  info->cache_writeback_granule = (uint8_t)BITS_SHIFT(ctr, 27, 24);
  info->idc = BIT(ctr, 28) == 0;  // inverted logic
  info->dic = BIT(ctr, 29) == 0;  // inverted logic

  for (int i = 0; i < 7; i++) {
    uint8_t ctype = (clidr >> (3 * i)) & 0x07;
    if (ctype == 0) {
      info->level_data_type[i].ctype = 0;
      info->level_inst_type[i].ctype = 0;
    } else if (ctype == 4) {                         // Unified
      __arm_wsr64("csselr_el1", (int64_t)(i << 1));  // Select cache level
      __isb(ARM_MB_SY);
      temp = __arm_rsr64("ccsidr_el1");
      info->level_data_type[i].ctype = 4;
      parse_ccsid(&(info->level_data_type[i]), temp);
    } else {
      if (ctype & 0x02) {
        __arm_wsr64("csselr_el1", (int64_t)(i << 1));
        __isb(ARM_MB_SY);
        temp = __arm_rsr64("ccsidr_el1");
        info->level_data_type[i].ctype = 2;
        parse_ccsid(&(info->level_data_type[i]), temp);
      }
      if (ctype & 0x01) {
        __arm_wsr64("csselr_el1", (int64_t)(i << 1) | 0x01);
        __isb(ARM_MB_SY);
        temp = __arm_rsr64("ccsidr_el1");
        info->level_inst_type[i].ctype = 1;
        parse_ccsid(&(info->level_inst_type[i]), temp);
      }
    }
  }
}

void arm64_dump_cache_info(cpu_num_t cpu) {
  arm64_cache_info_t* info = &(cache_info[cpu]);
  printf("==== ARM64 CACHE INFO CORE %u ====\n", cpu);
  printf("Inner Boundary = L%u\n", info->inner_boundary);
  printf("Level of Unification Uniprocessor = L%u\n", info->lou_u);
  printf("Level of Coherence = L%u\n", info->loc);
  printf("Level of Unification Inner Shareable = L%u\n", info->lou_is);
  printf("Instruction/Data cache minimum line = %u/%u\n", (1U << info->imin_line) * 4,
         (1U << info->dmin_line) * 4);
  printf("Cache Writeback Granule = %u\n", (1U << info->cache_writeback_granule) * 4);
  const char* icp = "";
  switch (info->l1_instruction_cache_policy) {
    case 0:
      icp = "VPIPT";
      break;
    case 1:
      icp = "AIVIVT";
      break;
    case 2:
      icp = "VIPT";
      break;
    case 3:
      icp = "PIPT";
      break;
  }
  printf("L1 Instruction cache policy = %s, ", icp);
  printf("IDC = %i, DIC = %i\n", info->idc, info->dic);
  for (int i = 0; i < 7; i++) {
    if ((info->level_data_type[i].ctype == 0) && (info->level_inst_type[i].ctype == 0)) {
      break;  // not implemented
    }
    printf("L%d Details:", i + 1);
    if (info->level_data_type[i].ctype == 4) {
      printf("\tUnified Cache, sets=%u, associativity=%u, line size=%u bytes\n",
             info->level_data_type[i].num_sets, info->level_data_type[i].associativity,
             info->level_data_type[i].line_size);
    } else {
      if (info->level_data_type[i].ctype & 0x02) {
        printf("\tData Cache, sets=%u, associativity=%u, line size=%u bytes\n",
               info->level_data_type[i].num_sets, info->level_data_type[i].associativity,
               info->level_data_type[i].line_size);
      }
      if (info->level_inst_type[i].ctype & 0x01) {
        if (info->level_data_type[i].ctype & 0x02) {
          printf("\t");
        }
        printf("\tInstruction Cache, sets=%u, associativity=%u, line size=%u bytes\n",
               info->level_inst_type[i].num_sets, info->level_inst_type[i].associativity,
               info->level_inst_type[i].line_size);
      }
    }
  }
}

enum arm64_microarch midr_to_microarch(uint32_t midr) {
  uint32_t implementer = BITS_SHIFT(midr, 31, 24);
  uint32_t partnum = BITS_SHIFT(midr, 15, 4);

  if (implementer == 'A') {
    // ARM cores
    switch (partnum) {
      case 0xd01:
        return ARM_CORTEX_A32;
      case 0xd03:
        return ARM_CORTEX_A53;
      case 0xd04:
        return ARM_CORTEX_A35;
      case 0xd05:
        return ARM_CORTEX_A55;
      case 0xd06:
        return ARM_CORTEX_A65;
      case 0xd07:
        return ARM_CORTEX_A57;
      case 0xd08:
        return ARM_CORTEX_A72;
      case 0xd09:
        return ARM_CORTEX_A73;
      case 0xd0a:
        return ARM_CORTEX_A75;
      case 0xd0b:
        return ARM_CORTEX_A76;
      case 0xd0c:
        return ARM_NEOVERSE_N1;
      case 0xd0d:
        return ARM_CORTEX_A77;
      case 0xd0e:
        return ARM_CORTEX_A76AE;
      case 0xd40:
        return ARM_NEOVERSE_V1;
      case 0xd41:
        return ARM_CORTEX_A78;
      case 0xd42:
        return ARM_CORTEX_A78AE;
      case 0xd44:
        return ARM_CORTEX_X1;
      case 0xd46:
        return ARM_CORTEX_A510;
      case 0xd47:
        return ARM_CORTEX_A710;
      case 0xd48:
        return ARM_CORTEX_X2;
      case 0xd49:
        return ARM_NEOVERSE_N2;
      case 0xd4a:
        return ARM_NEOVERSE_E1;
      case 0xd4b:
        return ARM_CORTEX_A78C;
      default:
        return UNKNOWN;
    }
  } else if (implementer == 'C') {
    // Cavium
    switch (partnum) {
      case 0xa1:
        return CAVIUM_CN88XX;
      case 0xaf:
        return CAVIUM_CN99XX;
      default:
        return UNKNOWN;
    }
  } else if (implementer == 0) {
    // software implementation
    switch (partnum) {
      case 0x51:
        return QEMU_TCG;
      default:
        return UNKNOWN;
    }
  } else {
    return UNKNOWN;
  }
}

static void midr_to_core_string(uint32_t midr, char* str, size_t len) {
  auto microarch = midr_to_microarch(midr);
  uint32_t implementer = BITS_SHIFT(midr, 31, 24);
  uint32_t variant = BITS_SHIFT(midr, 23, 20);
  __UNUSED uint32_t architecture = BITS_SHIFT(midr, 19, 16);
  uint32_t partnum = BITS_SHIFT(midr, 15, 4);
  uint32_t revision = BITS_SHIFT(midr, 3, 0);

  const char* partnum_str = "unknown";
  switch (microarch) {
    case ARM_CORTEX_A32:
      partnum_str = "ARM Cortex-A32";
      break;
    case ARM_CORTEX_A35:
      partnum_str = "ARM Cortex-A35";
      break;
    case ARM_CORTEX_A53:
      partnum_str = "ARM Cortex-A53";
      break;
    case ARM_CORTEX_A55:
      partnum_str = "ARM Cortex-A55";
      break;
    case ARM_CORTEX_A57:
      partnum_str = "ARM Cortex-A57";
      break;
    case ARM_CORTEX_A65:
      partnum_str = "ARM Cortex-A65";
      break;
    case ARM_CORTEX_A72:
      partnum_str = "ARM Cortex-A72";
      break;
    case ARM_CORTEX_A73:
      partnum_str = "ARM Cortex-A73";
      break;
    case ARM_CORTEX_A75:
      partnum_str = "ARM Cortex-A75";
      break;
    case ARM_CORTEX_A76:
      partnum_str = "ARM Cortex-A76";
      break;
    case ARM_CORTEX_A76AE:
      partnum_str = "ARM Cortex-A76AE";
      break;
    case ARM_CORTEX_A77:
      partnum_str = "ARM Cortex-A77";
      break;
    case ARM_CORTEX_A78:
      partnum_str = "ARM Cortex-A78";
      break;
    case ARM_CORTEX_A78AE:
      partnum_str = "ARM Cortex-A78AE";
      break;
    case ARM_CORTEX_A78C:
      partnum_str = "ARM Cortex-A78C";
      break;
    case ARM_CORTEX_A510:
      partnum_str = "ARM Cortex-A510";
      break;
    case ARM_CORTEX_A710:
      partnum_str = "ARM Cortex-A710";
      break;
    case ARM_CORTEX_X1:
      partnum_str = "ARM Cortex-X1";
      break;
    case ARM_CORTEX_X2:
      partnum_str = "ARM Cortex-X2";
      break;
    case ARM_NEOVERSE_E1:
      partnum_str = "ARM Neoverse E1";
      break;
    case ARM_NEOVERSE_N1:
      partnum_str = "ARM Neoverse N1";
      break;
    case ARM_NEOVERSE_N2:
      partnum_str = "ARM Neoverse N2";
      break;
    case ARM_NEOVERSE_V1:
      partnum_str = "ARM Neoverse V1";
      break;
    case CAVIUM_CN88XX:
      partnum_str = "Cavium CN88XX";
      break;
    case CAVIUM_CN99XX:
      partnum_str = "Cavium CN99XX";
      break;
    case QEMU_TCG:
      partnum_str = "QEMU TCG";
      break;
    case UNKNOWN: {
      const char i = (implementer ? (char)implementer : '0');
      snprintf(str, len, "Unknown implementer %c partnum 0x%x r%up%u", i, partnum, variant,
               revision);
      return;
    }
  }

  snprintf(str, len, "%s r%up%u", partnum_str, variant, revision);
}

static void print_cpu_info() {
  uint32_t midr = (uint32_t)__arm_rsr64("midr_el1");
  char cpu_name[128];
  midr_to_core_string(midr, cpu_name, sizeof(cpu_name));

  uint64_t mpidr = __arm_rsr64("mpidr_el1");

  dprintf(INFO, "ARM cpu %u: midr %#x '%s' mpidr %#" PRIx64 " aff %u:%u:%u:%u\n",
          arch_curr_cpu_num(), midr, cpu_name, mpidr,
          (uint32_t)((mpidr & MPIDR_AFF3_MASK) >> MPIDR_AFF3_SHIFT),
          (uint32_t)((mpidr & MPIDR_AFF2_MASK) >> MPIDR_AFF2_SHIFT),
          (uint32_t)((mpidr & MPIDR_AFF1_MASK) >> MPIDR_AFF1_SHIFT),
          (uint32_t)((mpidr & MPIDR_AFF0_MASK) >> MPIDR_AFF0_SHIFT));
}

bool arm64_feature_current_is_first_in_cluster() {
  const uint64_t mpidr = __arm_rsr64("mpidr_el1");
  return ((mpidr & MPIDR_AFF0_MASK) >> MPIDR_AFF0_SHIFT) == 0;
}

// call on every cpu to save features
void arm64_feature_init() {
  // set up some global constants based on the boot cpu
  cpu_num_t cpu = arch_curr_cpu_num();
  if (cpu == 0) {
    // read the block size of DC ZVA
    uint64_t dczid = __arm_rsr64("dczid_el0");
    uint32_t arm64_zva_shift = 0;
    if (BIT(dczid, 4) == 0) {
      arm64_zva_shift = (uint32_t)(__arm_rsr64("dczid_el0") & 0xf) + 2;
    }
    ASSERT(arm64_zva_shift != 0);  // for now, fail if DC ZVA is unavailable
    arm64_zva_size = (1u << arm64_zva_shift);

    // read the dcache and icache line size
    uint64_t ctr = __arm_rsr64("ctr_el0");
    uint32_t arm64_dcache_shift = (uint32_t)BITS_SHIFT(ctr, 19, 16) + 2;
    arm64_dcache_size = (1u << arm64_dcache_shift);
    uint32_t arm64_icache_shift = (uint32_t)BITS(ctr, 3, 0) + 2;
    arm64_icache_size = (1u << arm64_icache_shift);

    // parse the ISA feature bits
    arm64_isa_features |= ZX_HAS_CPU_FEATURES;

    auto isar0 = arch::ArmIdAa64IsaR0El1::Read();

    // Other values are reserved.  When assigned they will probably be
    // supersets of FEAT_PMULL, but don't presume that.
    switch (isar0.aes()) {
      case arch::ArmIdAa64IsaR0El1::Aes::kPmull:
        arm64_isa_features |= ZX_ARM64_FEATURE_ISA_PMULL;
        [[fallthrough]];
      case arch::ArmIdAa64IsaR0El1::Aes::kAes:
        arm64_isa_features |= ZX_ARM64_FEATURE_ISA_AES;
        break;
      case arch::ArmIdAa64IsaR0El1::Aes::kNone:
        break;
    }

    if (isar0.sha1() != arch::ArmIdAa64IsaR0El1::Sha1::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_SHA1;
    }
    if (isar0.sha2() != arch::ArmIdAa64IsaR0El1::Sha2::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_SHA2;
    }
    if (isar0.crc32() != arch::ArmIdAa64IsaR0El1::Crc32::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_CRC32;
    }
    if (isar0.atomic() != arch::ArmIdAa64IsaR0El1::Atomic::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_ATOMICS;
    }
    if (isar0.rdm() != arch::ArmIdAa64IsaR0El1::Rdm::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_RDM;
    }
    if (isar0.sha3() != arch::ArmIdAa64IsaR0El1::Sha3::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_SHA3;
    }
    if (isar0.sm3() != arch::ArmIdAa64IsaR0El1::Sm3::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_SM3;
    }
    if (isar0.sm4() != arch::ArmIdAa64IsaR0El1::Sm4::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_SM4;
    }
    if (isar0.dp() != arch::ArmIdAa64IsaR0El1::DotProd::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_DP;
    }
    if (isar0.fhm() != arch::ArmIdAa64IsaR0El1::Fhm::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_FHM;
    }
    if (isar0.ts() != arch::ArmIdAa64IsaR0El1::Ts::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_TS;
    }
    if (isar0.rndr() != arch::ArmIdAa64IsaR0El1::Rndr::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_RNDR;
    }

    auto isar1 = arch::ArmIdAa64IsaR1El1::Read();
    if (isar1.dpb() != arch::ArmIdAa64IsaR1El1::Dpb::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_DPB;
    }

    auto pfr0 = arch::ArmIdAa64Pfr0El1::Read();
    if (pfr0.fp() != arch::ArmIdAa64Pfr0El1::Fp::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_FP;
    }
    if (pfr0.advsimd() != arch::ArmIdAa64Pfr0El1::Fp::kNone) {
      arm64_isa_features |= ZX_ARM64_FEATURE_ISA_ASIMD;
    }

    // check the size of the asid
    uint64_t mmfr0 = __arm_rsr64("id_aa64mmfr0_el1");
    if ((mmfr0 & ARM64_MMFR0_ASIDBITS_MASK) == ARM64_MMFR0_ASIDBITS_16) {
      arm64_asid_width_ = arm64_asid_width::ASID_16;
    } else {
      arm64_asid_width_ = arm64_asid_width::ASID_8;
    }
  }

  // read the cache info for each cpu
  arm64_get_cache_info(&(cache_info[cpu]));
}

static void print_isa_features() {
  const struct {
    uint32_t bit;
    const char* name;
  } features[] = {
      {ZX_ARM64_FEATURE_ISA_FP, "fp"},       {ZX_ARM64_FEATURE_ISA_ASIMD, "asimd"},
      {ZX_ARM64_FEATURE_ISA_AES, "aes"},     {ZX_ARM64_FEATURE_ISA_PMULL, "pmull"},
      {ZX_ARM64_FEATURE_ISA_SHA1, "sha1"},   {ZX_ARM64_FEATURE_ISA_SHA2, "sha2"},
      {ZX_ARM64_FEATURE_ISA_CRC32, "crc32"}, {ZX_ARM64_FEATURE_ISA_ATOMICS, "atomics"},
      {ZX_ARM64_FEATURE_ISA_RDM, "rdm"},     {ZX_ARM64_FEATURE_ISA_SHA3, "sha3"},
      {ZX_ARM64_FEATURE_ISA_SM3, "sm3"},     {ZX_ARM64_FEATURE_ISA_SM4, "sm4"},
      {ZX_ARM64_FEATURE_ISA_DP, "dp"},       {ZX_ARM64_FEATURE_ISA_DPB, "dpb"},
      {ZX_ARM64_FEATURE_ISA_FHM, "fhm"},     {ZX_ARM64_FEATURE_ISA_TS, "ts"},
      {ZX_ARM64_FEATURE_ISA_RNDR, "rndr"},
  };

  printf("ARM ISA Features: ");
  uint col = 0;
  for (const auto& feature : features) {
    if (arm64_feature_test(feature.bit)) {
      col += printf("%s ", feature.name);
    }
    if (col >= 80) {
      printf("\n");
      col = 0;
    }
  }
  if (col > 0) {
    printf("\n");
  }
}

// dump the feature set
// print additional information if full is passed
void arm64_feature_debug(bool full) {
  print_cpu_info();

  if (full) {
    print_isa_features();
    dprintf(INFO, "ARM ASID width %s\n",
            (arm64_asid_width() == arm64_asid_width::ASID_16) ? "16" : "8");
    dprintf(INFO, "ARM cache line sizes: icache %u dcache %u zva %u\n", arm64_icache_size,
            arm64_dcache_size, arm64_zva_size);
    if (DPRINTF_ENABLED_FOR_LEVEL(INFO)) {
      arm64_dump_cache_info(arch_curr_cpu_num());
    }
  }
}
