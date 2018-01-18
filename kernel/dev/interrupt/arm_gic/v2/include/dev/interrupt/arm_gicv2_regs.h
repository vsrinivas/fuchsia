// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012-2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once
#include <reg.h>

extern uint64_t arm_gicv2_gic_base;
extern uint64_t arm_gicv2_gicd_offset;
extern uint64_t arm_gicv2_gicc_offset;
extern uint64_t arm_gicv2_gich_offset;
extern uint64_t arm_gicv2_gicv_offset;

#define GICREG(gic, reg) (*REG32(arm_gicv2_gic_base + (reg)))
#define GICD_OFFSET arm_gicv2_gicd_offset
#define GICC_OFFSET arm_gicv2_gicc_offset
#define GICH_OFFSET arm_gicv2_gich_offset
#define GICV_OFFSET arm_gicv2_gicv_offset

// CPU interface registers.
#define GICC_CTLR (GICC_OFFSET + 0x0000)
#define GICC_PMR (GICC_OFFSET + 0x0004)
#define GICC_BPR (GICC_OFFSET + 0x0008)
#define GICC_IAR (GICC_OFFSET + 0x000c)
#define GICC_EOIR (GICC_OFFSET + 0x0010)
#define GICC_RPR (GICC_OFFSET + 0x0014)
#define GICC_HPPIR (GICC_OFFSET + 0x0018)
#define GICC_APBR (GICC_OFFSET + 0x001c)
#define GICC_AIAR (GICC_OFFSET + 0x0020)
#define GICC_AEOIR (GICC_OFFSET + 0x0024)
#define GICC_AHPPIR (GICC_OFFSET + 0x0028)
#define GICC_APR(n) (GICC_OFFSET + 0x00d0 + (n)*4)
#define GICC_NSAPR(n) (GICC_OFFSET + 0x00e0 + (n)*4)
#define GICC_IIDR (GICC_OFFSET + 0x00fc)
#define GICC_DIR (GICC_OFFSET + 0x1000)

// Distributor registers.
#define GICD_CTLR (GICD_OFFSET + 0x000)
#define GICD_TYPER (GICD_OFFSET + 0x004)
#define GICD_IIDR (GICD_OFFSET + 0x008)
#define GICD_IGROUPR(n) (GICD_OFFSET + 0x080 + (n)*4)
#define GICD_ISENABLER(n) (GICD_OFFSET + 0x100 + (n)*4)
#define GICD_ICENABLER(n) (GICD_OFFSET + 0x180 + (n)*4)
#define GICD_ISPENDR(n) (GICD_OFFSET + 0x200 + (n)*4)
#define GICD_ICPENDR(n) (GICD_OFFSET + 0x280 + (n)*4)
#define GICD_ISACTIVER(n) (GICD_OFFSET + 0x300 + (n)*4)
#define GICD_ICACTIVER(n) (GICD_OFFSET + 0x380 + (n)*4)
#define GICD_IPRIORITYR(n) (GICD_OFFSET + 0x400 + (n)*4)
#define GICD_ITARGETSR(n) (GICD_OFFSET + 0x800 + (n)*4)
#define GICD_ICFGR(n) (GICD_OFFSET + 0xc00 + (n)*4)
#define GICD_NSACR(n) (GICD_OFFSET + 0xe00 + (n)*4)
#define GICD_SGIR (GICD_OFFSET + 0xf00)
#define GICD_CPENDSGIR(n) (GICD_OFFSET + 0xf10 + (n)*4)
#define GICD_SPENDSGIR(n) (GICD_OFFSET + 0xf20 + (n)*4)

#define GICD_CIDR0 (GICD_OFFSET + 0xff0)
#define GICD_CIDR1 (GICD_OFFSET + 0xff4)
#define GICD_CIDR2 (GICD_OFFSET + 0xff8)
#define GICD_CIDR3 (GICD_OFFSET + 0xffc)
#define GICD_PIDR0 (GICD_OFFSET + 0xfe0)
#define GICD_PIDR1 (GICD_OFFSET + 0xfe4)
#define GICD_PIDR2 (GICD_OFFSET + 0xfe8)
#define GICD_PIDR3 (GICD_OFFSET + 0xfec)

/* we might need to check that we're not a gic v3, in which case look for the v3 PIDR2 reg */
#define GICD_V3_PIDR2 (GICD_OFFSET + 0xffe8)

// Virtual interface control registers.
#define GICH_ADDRESS (GICH_OFFSET + arm_gicv2_gic_base)

// Virtual CPU interface registers.
#define GICV_ADDRESS (GICV_OFFSET + arm_gicv2_gic_base)

#define MAX_INT 1024
#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))
#define GIC_REG_COUNT(bit_per_reg) DIV_ROUND_UP(MAX_INT, (bit_per_reg))
#define DEFINE_GIC_SHADOW_REG(name, bit_per_reg, init_val, init_from) \
    uint32_t(name)[GIC_REG_COUNT(bit_per_reg)] = {                    \
        [(init_from / bit_per_reg)...(GIC_REG_COUNT(bit_per_reg) - 1)] = (init_val)}
