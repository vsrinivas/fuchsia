// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once
#include <arch/arm64.h>
#include <reg.h>

#define BIT_32(bit) (1u << bit)
#define BIT_64(bit) (1ul << bit)

#define GICREG(gic, reg) (*REG32(arm_gicv3_gic_base + (reg)))
#define GICREG64(gic, reg) (*REG64(arm_gicv3_gic_base + (reg)))
#define GICD_OFFSET arm_gicv3_gicd_offset
#define GICR_OFFSET arm_gicv3_gicr_offset
#define GICR_STRIDE arm_gicv3_gicr_stride

#define ICC_CTLR_EL1 "S3_0_C12_C12_4"
#define ICC_PMR_EL1 "S3_0_C4_C6_0"
#define ICC_IAR1_EL1 "S3_0_C12_C12_0"
#define ICC_SRE_EL1 "S3_0_C12_C12_5"
#define ICC_BPR1_EL1 "S3_0_C12_C12_3"
#define ICC_IGRPEN1_EL1 "S3_0_C12_C12_7"
#define ICC_EOIR1_EL1 "S3_0_C12_C12_1"
#define ICC_SGI1R_EL1 "S3_0_C12_C11_5"

/* distributor registers */

#define GICD_CTLR (GICD_OFFSET + 0x0000)
#define GICD_TYPER (GICD_OFFSET + 0x0004)
#define GICD_IIDR (GICD_OFFSET + 0x0008)
#define GICD_IGROUPR(n) (GICD_OFFSET + 0x0080 + (n)*4)
#define GICD_ISENABLER(n) (GICD_OFFSET + 0x0100 + (n)*4)
#define GICD_ICENABLER(n) (GICD_OFFSET + 0x0180 + (n)*4)
#define GICD_ISPENDR(n) (GICD_OFFSET + 0x0200 + (n)*4)
#define GICD_ICPENDR(n) (GICD_OFFSET + 0x0280 + (n)*4)
#define GICD_ISACTIVER(n) (GICD_OFFSET + 0x0300 + (n)*4)
#define GICD_ICACTIVER(n) (GICD_OFFSET + 0x0380 + (n)*4)
#define GICD_IPRIORITYR(n) (GICD_OFFSET + 0x0400 + (n)*4)
#define GICD_ITARGETSR(n) (GICD_OFFSET + 0x0800 + (n)*4)
#define GICD_ICFGR(n) (GICD_OFFSET + 0x0c00 + (n)*4)
#define GICD_IGRPMODR(n) (GICD_OFFSET + 0x0d00 + (n)*4)
#define GICD_NSACR(n) (GICD_OFFSET + 0x0e00 + (n)*4)
#define GICD_SGIR (GICD_OFFSET + 0x0f00)
#define GICD_CPENDSGIR(n) (GICD_OFFSET + 0x0f10 + (n)*4)
#define GICD_SPENDSGIR(n) (GICD_OFFSET + 0x0f20 + (n)*4)
#define GICD_IROUTER(n) (GICD_OFFSET + 0x6000 + (n)*8)

/* GICD_CTLR bit definitions */

#define CTLR_ENALBE_G0 BIT_32(0)
#define CTLR_ENABLE_G1NS BIT_32(1)
#define CTLR_ENABLE_G1S BIT_32(2)
#define CTLR_RES0 BIT_32(3)
#define CTLR_ARE_S BIT_32(4)
#define CTLR_ARE_NS BIT_32(5)
#define CTLR_DS BIT_32(6)
#define CTLR_E1NWF BIT_32(7)
#define GICD_CTLR_RWP BIT_32(31)

/* peripheral identification registers */

#define GICD_CIDR0 (GICD_OFFSET + 0xfff0)
#define GICD_CIDR1 (GICD_OFFSET + 0xfff4)
#define GICD_CIDR2 (GICD_OFFSET + 0xfff8)
#define GICD_CIDR3 (GICD_OFFSET + 0xfffc)
#define GICD_PIDR0 (GICD_OFFSET + 0xffe0)
#define GICD_PIDR1 (GICD_OFFSET + 0xffe4)
#define GICD_PIDR2 (GICD_OFFSET + 0xffe8)
#define GICD_PIDR3 (GICD_OFFSET + 0xffec)

/* GICD_PIDR bit definitions and masks */

#define GICD_PIDR2_ARCHREV_SHIFT 4
#define GICD_PIDR2_ARCHREV_MASK 0xf

/* redistributor registers */

#define GICR_SGI_OFFSET (GICR_OFFSET + 0x10000)

#define GICR_CTLR(i) (GICR_OFFSET + GICR_STRIDE * (i) + 0x0000)
#define GICR_IIDR(i) (GICR_OFFSET + GICR_STRIDE * (i) + 0x0004)
#define GICR_TYPER(i, n) (GICR_OFFSET + GICR_STRIDE * (i) + 0x0008 + (n)*4)
#define GICR_STATUSR(i) (GICR_OFFSET + GICR_STRIDE * (i) + 0x0010)
#define GICR_WAKER(i) (GICR_OFFSET + GICR_STRIDE * (i) + 0x0014)
#define GICR_IGROUPR0(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0080)
#define GICR_IGRPMOD0(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0d00)
#define GICR_ISENABLER0(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0100)
#define GICR_ICENABLER0(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0180)
#define GICR_ISPENDR0(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0200)
#define GICR_ICPENDR0(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0280)
#define GICR_ISACTIVER0(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0300)
#define GICR_ICACTIVER0(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0380)
#define GICR_IPRIORITYR0(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0400)
#define GICR_ICFGR0(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0c00)
#define GICR_ICFGR1(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0c04)
#define GICR_NSACR(i) (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0e00)

static inline void gic_write_ctlr(uint32_t val) {
    __asm__ volatile("msr " ICC_CTLR_EL1 ", %0" ::"r"((uint64_t)val));
    ISB;
}

static inline void gic_write_pmr(uint32_t val) {
    __asm__ volatile("msr " ICC_PMR_EL1 ", %0" ::"r"((uint64_t)val));
    ISB;
    DSB;
}

static inline void gic_write_igrpen(uint32_t val) {
    __asm__ volatile("msr " ICC_IGRPEN1_EL1 ", %0" ::"r"((uint64_t)val));
    ISB;
}

static inline uint32_t gic_read_sre(void) {
    uint64_t temp;
    __asm__ volatile("mrs %0, " ICC_SRE_EL1 : "=r"(temp));
    return (uint32_t)temp;
}

static inline void gic_write_sre(uint32_t val) {
    __asm__ volatile("msr " ICC_SRE_EL1 ", %0" ::"r"((uint64_t)val));
    ISB;
}

static inline void gic_write_eoir(uint32_t val) {
    __asm__ volatile("msr " ICC_EOIR1_EL1 ", %0" ::"r"((uint64_t)val));
    ISB;
}

static inline uint32_t gic_read_iar(void) {
    uint64_t temp;
    __asm__ volatile("mrs %0, " ICC_IAR1_EL1 : "=r"(temp));
    DSB;
    return (uint32_t)temp;
}

static inline void gic_write_sgi1r(uint64_t val) {
    __asm__ volatile("msr " ICC_SGI1R_EL1 ", %0" ::"r"((uint64_t)val));
    ISB;
    DSB;
}