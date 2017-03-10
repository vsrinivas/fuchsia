// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012-2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <platform/gic.h>
#include <reg.h>

#define GICREG(gic, reg) (*REG32(GICBASE(gic) + (reg)))

#if ARM_GIC_V3

/* GIC V3 system registers */

#define DEFINE_ICC_SYS_REG(name, reg)                   \
static inline uint32_t gic_read_##name(void) {          \
    uint64_t temp;                                      \
   __asm__ volatile("mrs %0, " reg : "=r"(temp));       \
    return temp;                                        \
}                                                       \
static inline void gic_write_##name(uint32_t value) {   \
   __asm__ volatile("msr " reg ", %0" :: "r"((uint64_t)value));   \
}

DEFINE_ICC_SYS_REG(ctlr, "S3_0_C12_C12_4")
DEFINE_ICC_SYS_REG(pmr, "S3_0_C4_C6_0")
DEFINE_ICC_SYS_REG(iar1, "S3_0_C12_C12_0")
DEFINE_ICC_SYS_REG(sre, "S3_0_C12_C12_5")
DEFINE_ICC_SYS_REG(bpr1, "S3_0_C12_C12_3")
DEFINE_ICC_SYS_REG(igrpen1, "S3_0_C12_C12_7")
DEFINE_ICC_SYS_REG(eoir1, "S3_0_C12_C12_1")

#else

/* main cpu regs */
#define GICC_CTLR               (GICC_OFFSET + 0x0000)
#define GICC_PMR                (GICC_OFFSET + 0x0004)
#define GICC_BPR                (GICC_OFFSET + 0x0008)
#define GICC_IAR                (GICC_OFFSET + 0x000c)
#define GICC_EOIR               (GICC_OFFSET + 0x0010)
#define GICC_RPR                (GICC_OFFSET + 0x0014)
#define GICC_HPPIR              (GICC_OFFSET + 0x0018)
#define GICC_APBR               (GICC_OFFSET + 0x001c)
#define GICC_AIAR               (GICC_OFFSET + 0x0020)
#define GICC_AEOIR              (GICC_OFFSET + 0x0024)
#define GICC_AHPPIR             (GICC_OFFSET + 0x0028)
#define GICC_APR(n)             (GICC_OFFSET + 0x00d0 + (n) * 4)
#define GICC_NSAPR(n)           (GICC_OFFSET + 0x00e0 + (n) * 4)
#define GICC_IIDR               (GICC_OFFSET + 0x00fc)
#define GICC_DIR                (GICC_OFFSET + 0x1000)

#endif // ARM_GIC_V3

/* distribution regs */
#define GICD_CTLR               (GICD_OFFSET + 0x000)
#define GICD_TYPER              (GICD_OFFSET + 0x004)
#define GICD_IIDR               (GICD_OFFSET + 0x008)
#define GICD_IGROUPR(n)         (GICD_OFFSET + 0x080 + (n) * 4)
#define GICD_ISENABLER(n)       (GICD_OFFSET + 0x100 + (n) * 4)
#define GICD_ICENABLER(n)       (GICD_OFFSET + 0x180 + (n) * 4)
#define GICD_ISPENDR(n)         (GICD_OFFSET + 0x200 + (n) * 4)
#define GICD_ICPENDR(n)         (GICD_OFFSET + 0x280 + (n) * 4)
#define GICD_ISACTIVER(n)       (GICD_OFFSET + 0x300 + (n) * 4)
#define GICD_ICACTIVER(n)       (GICD_OFFSET + 0x380 + (n) * 4)
#define GICD_IPRIORITYR(n)      (GICD_OFFSET + 0x400 + (n) * 4)
#define GICD_ITARGETSR(n)       (GICD_OFFSET + 0x800 + (n) * 4)
#define GICD_ICFGR(n)           (GICD_OFFSET + 0xc00 + (n) * 4)
#define GICD_NSACR(n)           (GICD_OFFSET + 0xe00 + (n) * 4)
#define GICD_SGIR               (GICD_OFFSET + 0xf00)
#define GICD_CPENDSGIR(n)       (GICD_OFFSET + 0xf10 + (n) * 4)
#define GICD_SPENDSGIR(n)       (GICD_OFFSET + 0xf20 + (n) * 4)

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define GIC_REG_COUNT(bit_per_reg) DIV_ROUND_UP(MAX_INT, (bit_per_reg))
#define DEFINE_GIC_SHADOW_REG(name, bit_per_reg, init_val, init_from) \
    uint32_t (name)[GIC_REG_COUNT(bit_per_reg)] = { \
        [(init_from / bit_per_reg) ... \
         (GIC_REG_COUNT(bit_per_reg) - 1)] = (init_val) \
    }

