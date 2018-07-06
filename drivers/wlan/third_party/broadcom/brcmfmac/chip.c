/*
 * Copyright (c) 2014 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "chip.h"

#include <zircon/listnode.h>
#include <zircon/status.h>

#include "brcm_hw_ids.h"
#include "brcmu_utils.h"
#include "chipcommon.h"
#include "debug.h"
#include "defs.h"
#include "device.h"
#include "linuxisms.h"
#include "soc.h"

/* SOC Interconnect types (aka chip types) */
#define SOCI_SB 0
#define SOCI_AI 1

// clang-format off

/* PL-368 DMP definitions */
#define DMP_DESC_TYPE_MSK       0x0000000F
#define DMP_DESC_EMPTY          0x00000000
#define DMP_DESC_VALID          0x00000001
#define DMP_DESC_COMPONENT      0x00000001
#define DMP_DESC_MASTER_PORT    0x00000003
#define DMP_DESC_ADDRESS        0x00000005
#define DMP_DESC_ADDRSIZE_GT32  0x00000008
#define DMP_DESC_EOT            0x0000000F

#define DMP_COMP_DESIGNER     0xFFF00000
#define DMP_COMP_DESIGNER_S   20
#define DMP_COMP_PARTNUM      0x000FFF00
#define DMP_COMP_PARTNUM_S    8
#define DMP_COMP_CLASS        0x000000F0
#define DMP_COMP_CLASS_S      4
#define DMP_COMP_REVISION     0xFF000000
#define DMP_COMP_REVISION_S   24
#define DMP_COMP_NUM_SWRAP    0x00F80000
#define DMP_COMP_NUM_SWRAP_S  19
#define DMP_COMP_NUM_MWRAP    0x0007C000
#define DMP_COMP_NUM_MWRAP_S  14
#define DMP_COMP_NUM_SPORT    0x00003E00
#define DMP_COMP_NUM_SPORT_S  9
#define DMP_COMP_NUM_MPORT    0x000001F0
#define DMP_COMP_NUM_MPORT_S  4

#define DMP_MASTER_PORT_UID   0x0000FF00
#define DMP_MASTER_PORT_UID_S 8
#define DMP_MASTER_PORT_NUM   0x000000F0
#define DMP_MASTER_PORT_NUM_S 4

#define DMP_SLAVE_ADDR_BASE    0xFFFFF000
#define DMP_SLAVE_ADDR_BASE_S  12
#define DMP_SLAVE_PORT_NUM     0x00000F00
#define DMP_SLAVE_PORT_NUM_S   8
#define DMP_SLAVE_TYPE         0x000000C0
#define DMP_SLAVE_TYPE_S       6
#define DMP_SLAVE_TYPE_SLAVE   0
#define DMP_SLAVE_TYPE_BRIDGE  1
#define DMP_SLAVE_TYPE_SWRAP   2
#define DMP_SLAVE_TYPE_MWRAP   3
#define DMP_SLAVE_SIZE_TYPE    0x00000030
#define DMP_SLAVE_SIZE_TYPE_S  4
#define DMP_SLAVE_SIZE_4K      0
#define DMP_SLAVE_SIZE_8K      1
#define DMP_SLAVE_SIZE_16K     2
#define DMP_SLAVE_SIZE_DESC    3

/* EROM CompIdentB */
#define CIB_REV_MASK  0xff000000
#define CIB_REV_SHIFT 24

/* ARM CR4 core specific control flag bits */
#define ARMCR4_BCMA_IOCTL_CPUHALT 0x0020

/* D11 core specific control flag bits */
#define D11_BCMA_IOCTL_PHYCLOCKEN 0x0004
#define D11_BCMA_IOCTL_PHYRESET   0x0008

/* chip core base & ramsize */
/* bcm4329 */
/* SDIO device core, ID 0x829 */
#define BCM4329_CORE_BUS_BASE 0x18011000
/* internal memory core, ID 0x80e */
#define BCM4329_CORE_SOCRAM_BASE 0x18003000
/* ARM Cortex M3 core, ID 0x82a */
#define BCM4329_CORE_ARM_BASE 0x18002000

/* Max possibly supported memory size (limited by IO mapped memory) */
#define BRCMF_CHIP_MAX_MEMSIZE (4 * 1024 * 1024)

#define CORE_SB(base, field) (base + SBCONFIGOFF + offsetof(struct sbconfig, field))
#define SBCOREREV(sbidh)                                                                  \
    ((((sbidh)&BACKPLANE_ID_HIGH_REVCODE_HIGH) >> BACKPLANE_ID_HIGH_REVCODE_HIGH_SHIFT) | \
      ((sbidh)&BACKPLANE_ID_HIGH_REVCODE_LOW))

struct sbconfig {
    uint32_t PAD[2];
    uint32_t sbipsflag; /* initiator port ocp slave flag */
    uint32_t PAD[3];
    uint32_t sbtpsflag; /* target port ocp slave flag */
    uint32_t PAD[11];
    uint32_t sbtmerrloga; /* (sonics >= 2.3) */
    uint32_t PAD;
    uint32_t sbtmerrlog; /* (sonics >= 2.3) */
    uint32_t PAD[3];
    uint32_t sbadmatch3; /* address match3 */
    uint32_t PAD;
    uint32_t sbadmatch2; /* address match2 */
    uint32_t PAD;
    uint32_t sbadmatch1; /* address match1 */
    uint32_t PAD[7];
    uint32_t sbimstate;     /* initiator agent state */
    uint32_t sbintvec;      /* interrupt mask */
    uint32_t sbtmstatelow;  /* target state */
    uint32_t sbtmstatehigh; /* target state */
    uint32_t sbbwa0;        /* bandwidth allocation table0 */
    uint32_t PAD;
    uint32_t sbimconfiglow;  /* initiator configuration */
    uint32_t sbimconfighigh; /* initiator configuration */
    uint32_t sbadmatch0;     /* address match0 */
    uint32_t PAD;
    uint32_t sbtmconfiglow;  /* target configuration */
    uint32_t sbtmconfighigh; /* target configuration */
    uint32_t sbbconfig;      /* broadcast configuration */
    uint32_t PAD;
    uint32_t sbbstate; /* broadcast state */
    uint32_t PAD[3];
    uint32_t sbactcnfg; /* activate configuration */
    uint32_t PAD[3];
    uint32_t sbflagst; /* current sbflags */
    uint32_t PAD[3];
    uint32_t sbidlow;  /* identification */
    uint32_t sbidhigh; /* identification */
};

/* bankidx and bankinfo reg defines corerev >= 8 */
#define SOCRAM_BANKINFO_RETNTRAM_MASK 0x00010000
#define SOCRAM_BANKINFO_SZMASK        0x0000007f
#define SOCRAM_BANKIDX_ROM_MASK       0x00000100

#define SOCRAM_BANKIDX_MEMTYPE_SHIFT 8
/* socram bankinfo memtype */
#define SOCRAM_MEMTYPE_RAM 0
#define SOCRAM_MEMTYPE_R0M 1
#define SOCRAM_MEMTYPE_DEVRAM 2

#define SOCRAM_BANKINFO_SZBASE 8192
#define SRCI_LSS_MASK    0x00f00000
#define SRCI_LSS_SHIFT   20
#define SRCI_SRNB_MASK         0xf0
#define SRCI_SRNB_SHIFT  4
#define SRCI_SRBSZ_MASK         0xf
#define SRCI_SRBSZ_SHIFT          0
#define SR_BSZ_BASE 14

struct sbsocramregs {
    uint32_t coreinfo;
    uint32_t bwalloc;
    uint32_t extracoreinfo;
    uint32_t biststat;
    uint32_t bankidx;
    uint32_t standbyctrl;

    uint32_t errlogstatus; /* rev 6 */
    uint32_t errlogaddr;   /* rev 6 */
    /* used for patching rev 3 & 5 */
    uint32_t cambankidx;
    uint32_t cambankstandbyctrl;
    uint32_t cambankpatchctrl;
    uint32_t cambankpatchtblbaseaddr;
    uint32_t cambankcmdreg;
    uint32_t cambankdatareg;
    uint32_t cambankmaskreg;
    uint32_t PAD[1];
    uint32_t bankinfo; /* corev 8 */
    uint32_t bankpda;
    uint32_t PAD[14];
    uint32_t extmemconfig;
    uint32_t extmemparitycsr;
    uint32_t extmemparityerrdata;
    uint32_t extmemparityerrcnt;
    uint32_t extmemwrctrlandsize;
    uint32_t PAD[84];
    uint32_t workaround;
    uint32_t pwrctl; /* corerev >= 2 */
    uint32_t PAD[133];
    uint32_t sr_control; /* corerev >= 15 */
    uint32_t sr_status;  /* corerev >= 15 */
    uint32_t sr_address; /* corerev >= 15 */
    uint32_t sr_data;    /* corerev >= 15 */
};

#define SOCRAMREGOFFS(_f) offsetof(struct sbsocramregs, _f)
#define SYSMEMREGOFFS(_f) offsetof(struct sbsocramregs, _f)

#define ARMCR4_CAP      (0x04)
#define ARMCR4_BANKIDX  (0x40)
#define ARMCR4_BANKINFO (0x44)
#define ARMCR4_BANKPDA  (0x4C)

#define ARMCR4_TCBBNB_MASK  0xf0
#define ARMCR4_TCBBNB_SHIFT 4
#define ARMCR4_TCBANB_MASK  0x0f
#define ARMCR4_TCBANB_SHIFT 0

#define ARMCR4_BSZ_MASK 0x3f
#define ARMCR4_BSZ_MULT 8192

// clang-format on

struct brcmf_core_priv {
    struct brcmf_core pub;
    uint32_t wrapbase;
    struct list_node list;
    struct brcmf_chip_priv* chip;
};

struct brcmf_chip_priv {
    struct brcmf_chip pub;
    const struct brcmf_buscore_ops* ops;
    void* ctx;
    /* assured first core is chipcommon, second core is buscore */
    struct list_node cores;
    uint16_t num_cores;

    bool (*iscoreup)(struct brcmf_core_priv* core);
    void (*coredisable)(struct brcmf_core_priv* core, uint32_t prereset, uint32_t reset);
    void (*resetcore)(struct brcmf_core_priv* core, uint32_t prereset, uint32_t reset,
                      uint32_t postreset);
};

static void brcmf_chip_sb_corerev(struct brcmf_chip_priv* ci, struct brcmf_core* core) {
    uint32_t regdata;

    regdata = ci->ops->read32(ci->ctx, CORE_SB(core->base, sbidhigh));
    core->rev = SBCOREREV(regdata);
}

static bool brcmf_chip_sb_iscoreup(struct brcmf_core_priv* core) {
    struct brcmf_chip_priv* ci;
    uint32_t regdata;
    uint32_t address;

    ci = core->chip;
    address = CORE_SB(core->pub.base, sbtmstatelow);
    regdata = ci->ops->read32(ci->ctx, address);
    regdata &= (BACKPLANE_TARGET_STATE_LOW_RESET | BACKPLANE_TARGET_STATE_LOW_REJECT |
        BACKPLANE_INITIATOR_STATE_REJECT | BACKPLANE_TARGET_STATE_LOW_CLOCK);
    return BACKPLANE_TARGET_STATE_LOW_CLOCK == regdata;
}

static bool brcmf_chip_ai_iscoreup(struct brcmf_core_priv* core) {
    struct brcmf_chip_priv* ci;
    uint32_t regdata;
    bool ret;

    ci = core->chip;
    regdata = ci->ops->read32(ci->ctx, core->wrapbase + BC_CORE_CONTROL);
    ret = (regdata & (BC_CORE_CONTROL_FGC | BC_CORE_CONTROL_CLOCK)) == BC_CORE_CONTROL_CLOCK;

    regdata = ci->ops->read32(ci->ctx, core->wrapbase + BC_CORE_RESET_CONTROL);
    ret = ret && ((regdata & BC_CORE_RESET_CONTROL_RESET) == 0);

    return ret;
}

static void brcmf_chip_sb_coredisable(struct brcmf_core_priv* core, uint32_t prereset,
                                      uint32_t reset) {
    struct brcmf_chip_priv* ci;
    uint32_t val, base;

    ci = core->chip;
    base = core->pub.base;
    val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
    if (val & BACKPLANE_TARGET_STATE_LOW_RESET) {
        return;
    }

    val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
    if ((val & BACKPLANE_TARGET_STATE_LOW_CLOCK) != 0) {
        /*
         * set target reject and spin until busy is clear
         * (preserve core-specific bits)
         */
        val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
        ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow), val |
            BACKPLANE_TARGET_STATE_LOW_REJECT);

        val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
        usleep(1);
        SPINWAIT((ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatehigh)) &
                  BACKPLANE_TARGET_STATE_HIGH_BUSY),
                 100000);

        val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatehigh));
        if (val & BACKPLANE_TARGET_STATE_HIGH_BUSY) {
            brcmf_err("core state still busy\n");
        }

        val = ci->ops->read32(ci->ctx, CORE_SB(base, sbidlow));
        if (val & BACKPLANE_ID_LOW_INITIATOR) {
            val = ci->ops->read32(ci->ctx, CORE_SB(base, sbimstate));
            val |= BACKPLANE_INITIATOR_STATE_REJECT;
            ci->ops->write32(ci->ctx, CORE_SB(base, sbimstate), val);
            val = ci->ops->read32(ci->ctx, CORE_SB(base, sbimstate));
            usleep(1);
            SPINWAIT((ci->ops->read32(ci->ctx, CORE_SB(base, sbimstate)) &
                      BACKPLANE_INITIATOR_STATE_BUSY),
                     100000);
        }

        /* set reset and reject while enabling the clocks */
        val = BACKPLANE_TARGET_STATE_LOW_GATED_CLOCKS | BACKPLANE_TARGET_STATE_LOW_CLOCK |
            BACKPLANE_TARGET_STATE_LOW_REJECT | BACKPLANE_TARGET_STATE_LOW_RESET;
        ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow), val);
        val = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
        usleep(10);

        /* clear the initiator reject bit */
        val = ci->ops->read32(ci->ctx, CORE_SB(base, sbidlow));
        if (val & BACKPLANE_ID_LOW_INITIATOR) {
            val = ci->ops->read32(ci->ctx, CORE_SB(base, sbimstate));
            val &= ~BACKPLANE_INITIATOR_STATE_REJECT;
            ci->ops->write32(ci->ctx, CORE_SB(base, sbimstate), val);
        }
    }

    /* leave reset and reject asserted */
    ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow), (BACKPLANE_TARGET_STATE_LOW_REJECT |
                                                            BACKPLANE_TARGET_STATE_LOW_RESET));
    usleep(1);
}

static void brcmf_chip_ai_coredisable(struct brcmf_core_priv* core, uint32_t prereset,
                                      uint32_t reset) {
    struct brcmf_chip_priv* ci;
    uint32_t regdata;

    ci = core->chip;

    /* if core is already in reset, skip reset */
    regdata = ci->ops->read32(ci->ctx, core->wrapbase + BC_CORE_RESET_CONTROL);
    if ((regdata & BC_CORE_RESET_CONTROL_RESET) != 0) {
        goto in_reset_configure;
    }

    /* configure reset */
    ci->ops->write32(ci->ctx, core->wrapbase + BC_CORE_CONTROL,
                     prereset | BC_CORE_CONTROL_FGC | BC_CORE_CONTROL_CLOCK);
    ci->ops->read32(ci->ctx, core->wrapbase + BC_CORE_CONTROL);

    /* put in reset */
    ci->ops->write32(ci->ctx, core->wrapbase + BC_CORE_RESET_CONTROL, BC_CORE_RESET_CONTROL_RESET);
    usleep_range(10, 20);
    brcmf_dbg(TEMP, "About to wait");
    /* wait till reset is 1 */
    SPINWAIT(ci->ops->read32(ci->ctx, core->wrapbase + BC_CORE_RESET_CONTROL) !=
             BC_CORE_RESET_CONTROL_RESET,
             300);
    brcmf_dbg(TEMP, "Survived wait");
in_reset_configure:
    /* in-reset configure */
    ci->ops->write32(ci->ctx, core->wrapbase + BC_CORE_CONTROL, reset | BC_CORE_CONTROL_FGC |
                     BC_CORE_CONTROL_CLOCK);
    ci->ops->read32(ci->ctx, core->wrapbase + BC_CORE_CONTROL);
}

static void brcmf_chip_sb_resetcore(struct brcmf_core_priv* core, uint32_t prereset, uint32_t reset,
                                    uint32_t postreset) {
    struct brcmf_chip_priv* ci;
    uint32_t regdata;
    uint32_t base;

    ci = core->chip;
    base = core->pub.base;
    /*
     * Must do the disable sequence first to work for
     * arbitrary current core state.
     */
    brcmf_chip_sb_coredisable(core, 0, 0);

    /*
     * Now do the initialization sequence.
     * set reset while enabling the clock and
     * forcing them on throughout the core
     */
    ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow),
                     BACKPLANE_TARGET_STATE_LOW_GATED_CLOCKS | BACKPLANE_TARGET_STATE_LOW_CLOCK |
                     BACKPLANE_TARGET_STATE_LOW_RESET);
    regdata = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
    usleep(1);

    /* clear any serror */
    regdata = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatehigh));
    if (regdata & BACKPLANE_TARGET_STATE_HIGH_S_ERROR) {
        ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatehigh), 0);
    }

    regdata = ci->ops->read32(ci->ctx, CORE_SB(base, sbimstate));
    if (regdata & (BACKPLANE_INITIATOR_STATE_IN_BAND_ERROR | BACKPLANE_INITIATOR_STATE_TIMEOUT)) {
        regdata &= ~(BACKPLANE_INITIATOR_STATE_IN_BAND_ERROR | BACKPLANE_INITIATOR_STATE_TIMEOUT);
        ci->ops->write32(ci->ctx, CORE_SB(base, sbimstate), regdata);
    }

    /* clear reset and allow it to propagate throughout the core */
    ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow), BACKPLANE_TARGET_STATE_LOW_GATED_CLOCKS |
                                                           BACKPLANE_TARGET_STATE_LOW_CLOCK);
    regdata = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
    usleep(1);

    /* leave clock enabled */
    ci->ops->write32(ci->ctx, CORE_SB(base, sbtmstatelow), BACKPLANE_TARGET_STATE_LOW_CLOCK);
    regdata = ci->ops->read32(ci->ctx, CORE_SB(base, sbtmstatelow));
    usleep(1);
}

static void brcmf_chip_ai_resetcore(struct brcmf_core_priv* core, uint32_t prereset, uint32_t reset,
                                    uint32_t postreset) {
    struct brcmf_chip_priv* ci;
    int count;

    ci = core->chip;

    /* must disable first to work for arbitrary current core state */
    brcmf_chip_ai_coredisable(core, prereset, reset);

    count = 0;
    while (ci->ops->read32(ci->ctx, core->wrapbase + BC_CORE_RESET_CONTROL) &
           BC_CORE_RESET_CONTROL_RESET) {
        ci->ops->write32(ci->ctx, core->wrapbase + BC_CORE_RESET_CONTROL, 0);
        count++;
        if (count > 50) {
            break;
        }
        usleep_range(40, 60);
    }

    ci->ops->write32(ci->ctx, core->wrapbase + BC_CORE_CONTROL, postreset | BC_CORE_CONTROL_CLOCK);
    ci->ops->read32(ci->ctx, core->wrapbase + BC_CORE_CONTROL);
}

static char* brcmf_chip_name(uint chipid, char* buf, uint len) {
    const char* fmt;

    fmt = ((chipid > 0xa000) || (chipid < 0x4000)) ? "%d" : "%x";
    snprintf(buf, len, fmt, chipid);
    return buf;
}

static zx_status_t brcmf_chip_add_core(struct brcmf_chip_priv* ci, uint16_t coreid, uint32_t base,
                                       uint32_t wrapbase, struct brcmf_core** core_out) {
    struct brcmf_core_priv* core;

    core = calloc(1, sizeof(*core));
    if (!core) {
        if (core_out) {
            *core_out = NULL;
        }
        return ZX_ERR_NO_MEMORY;
    }

    core->pub.id = coreid;
    core->pub.base = base;
    core->chip = ci;
    core->wrapbase = wrapbase;

    list_add_tail(&ci->cores, &core->list);
    if (core_out) {
        *core_out = &core->pub;
    }
    return ZX_OK;
}

/* safety check for chipinfo */
static zx_status_t brcmf_chip_cores_check(struct brcmf_chip_priv* ci) {
    struct brcmf_core_priv* core;
    bool need_socram = false;
    bool has_socram = false;
    bool cpu_found = false;
    int idx = 1;

    list_for_each_entry(core, &ci->cores, list) {
        brcmf_dbg(INFO, " [%-2d] core 0x%x:%-2d base 0x%08x wrap 0x%08x\n", idx++, core->pub.id,
                  core->pub.rev, core->pub.base, core->wrapbase);

        switch (core->pub.id) {
        case CHIPSET_ARM_CM3_CORE:
            cpu_found = true;
            need_socram = true;
            break;
        case CHIPSET_INTERNAL_MEM_CORE:
            has_socram = true;
            break;
        case CHIPSET_ARM_CR4_CORE:
            cpu_found = true;
            break;
        case CHIPSET_ARM_CA7_CORE:
            cpu_found = true;
            break;
        default:
            break;
        }
    }

    if (!cpu_found) {
        brcmf_err("CPU core not detected\n");
        return ZX_ERR_IO_NOT_PRESENT;
    }
    /* check RAM core presence for ARM CM3 core */
    if (need_socram && !has_socram) {
        brcmf_err("RAM core not provided with ARM CM3 core\n");
        return ZX_ERR_WRONG_TYPE;
    }
    return ZX_OK;
}

static uint32_t brcmf_chip_core_read32(struct brcmf_core_priv* core, uint16_t reg) {
    return core->chip->ops->read32(core->chip->ctx, core->pub.base + reg);
}

static void brcmf_chip_core_write32(struct brcmf_core_priv* core, uint16_t reg, uint32_t val) {
    core->chip->ops->write32(core->chip->ctx, core->pub.base + reg, val);
}

static bool brcmf_chip_socram_banksize(struct brcmf_core_priv* core, uint8_t idx,
                                       uint32_t* banksize) {
    uint32_t bankinfo;
    uint32_t bankidx = (SOCRAM_MEMTYPE_RAM << SOCRAM_BANKIDX_MEMTYPE_SHIFT);

    bankidx |= idx;
    brcmf_chip_core_write32(core, SOCRAMREGOFFS(bankidx), bankidx);
    bankinfo = brcmf_chip_core_read32(core, SOCRAMREGOFFS(bankinfo));
    *banksize = (bankinfo & SOCRAM_BANKINFO_SZMASK) + 1;
    *banksize *= SOCRAM_BANKINFO_SZBASE;
    return !!(bankinfo & SOCRAM_BANKINFO_RETNTRAM_MASK);
}

static void brcmf_chip_socram_ramsize(struct brcmf_core_priv* sr, uint32_t* ramsize,
                                      uint32_t* srsize) {
    uint32_t coreinfo;
    uint nb, banksize, lss;
    bool retent;
    int i;

    *ramsize = 0;
    *srsize = 0;

    if (WARN_ON(sr->pub.rev < 4)) {
        return;
    }

    if (!brcmf_chip_iscoreup(&sr->pub)) {
        brcmf_chip_resetcore(&sr->pub, 0, 0, 0);
    }

    /* Get info for determining size */
    coreinfo = brcmf_chip_core_read32(sr, SOCRAMREGOFFS(coreinfo));
    nb = (coreinfo & SRCI_SRNB_MASK) >> SRCI_SRNB_SHIFT;

    if ((sr->pub.rev <= 7) || (sr->pub.rev == 12)) {
        banksize = (coreinfo & SRCI_SRBSZ_MASK);
        lss = (coreinfo & SRCI_LSS_MASK) >> SRCI_LSS_SHIFT;
        if (lss != 0) {
            nb--;
        }
        *ramsize = nb * (1 << (banksize + SR_BSZ_BASE));
        if (lss != 0) {
            *ramsize += (1 << ((lss - 1) + SR_BSZ_BASE));
        }
    } else {
        nb = (coreinfo & SRCI_SRNB_MASK) >> SRCI_SRNB_SHIFT;
        for (i = 0; i < (int)nb; i++) {
            retent = brcmf_chip_socram_banksize(sr, i, &banksize);
            *ramsize += banksize;
            if (retent) {
                *srsize += banksize;
            }
        }
    }

    /* hardcoded save&restore memory sizes */
    switch (sr->chip->pub.chip) {
    case BRCM_CC_4334_CHIP_ID:
        if (sr->chip->pub.chiprev < 2) {
            *srsize = (32 * 1024);
        }
        break;
    case BRCM_CC_43430_CHIP_ID:
        /* assume sr for now as we can not check
         * firmware sr capability at this point.
         */
        *srsize = (64 * 1024);
        break;
    default:
        break;
    }
}

/** Return the SYS MEM size */
static uint32_t brcmf_chip_sysmem_ramsize(struct brcmf_core_priv* sysmem) {
    uint32_t memsize = 0;
    uint32_t coreinfo;
    uint32_t idx;
    uint32_t nb;
    uint32_t banksize;

    if (!brcmf_chip_iscoreup(&sysmem->pub)) {
        brcmf_chip_resetcore(&sysmem->pub, 0, 0, 0);
    }

    coreinfo = brcmf_chip_core_read32(sysmem, SYSMEMREGOFFS(coreinfo));
    nb = (coreinfo & SRCI_SRNB_MASK) >> SRCI_SRNB_SHIFT;

    for (idx = 0; idx < nb; idx++) {
        brcmf_chip_socram_banksize(sysmem, idx, &banksize);
        memsize += banksize;
    }

    return memsize;
}

/** Return the TCM-RAM size of the ARMCR4 core. */
static uint32_t brcmf_chip_tcm_ramsize(struct brcmf_core_priv* cr4) {
    uint32_t corecap;
    uint32_t memsize = 0;
    uint32_t nab;
    uint32_t nbb;
    uint32_t totb;
    uint32_t bxinfo;
    uint32_t idx;

    corecap = brcmf_chip_core_read32(cr4, ARMCR4_CAP);

    nab = (corecap & ARMCR4_TCBANB_MASK) >> ARMCR4_TCBANB_SHIFT;
    nbb = (corecap & ARMCR4_TCBBNB_MASK) >> ARMCR4_TCBBNB_SHIFT;
    totb = nab + nbb;

    for (idx = 0; idx < totb; idx++) {
        brcmf_chip_core_write32(cr4, ARMCR4_BANKIDX, idx);
        bxinfo = brcmf_chip_core_read32(cr4, ARMCR4_BANKINFO);
        memsize += ((bxinfo & ARMCR4_BSZ_MASK) + 1) * ARMCR4_BSZ_MULT;
    }

    return memsize;
}

static uint32_t brcmf_chip_tcm_rambase(struct brcmf_chip_priv* ci) {
    switch (ci->pub.chip) {
    case BRCM_CC_4345_CHIP_ID:
        return 0x198000;
    case BRCM_CC_4335_CHIP_ID:
    case BRCM_CC_4339_CHIP_ID:
    case BRCM_CC_4350_CHIP_ID:
    case BRCM_CC_4354_CHIP_ID:
    case BRCM_CC_4356_CHIP_ID:
    case BRCM_CC_43567_CHIP_ID:
    case BRCM_CC_43569_CHIP_ID:
    case BRCM_CC_43570_CHIP_ID:
    case BRCM_CC_4358_CHIP_ID:
    case BRCM_CC_4359_CHIP_ID:
    case BRCM_CC_43602_CHIP_ID:
    case BRCM_CC_4371_CHIP_ID:
        return 0x180000;
    case BRCM_CC_43465_CHIP_ID:
    case BRCM_CC_43525_CHIP_ID:
    case BRCM_CC_4365_CHIP_ID:
    case BRCM_CC_4366_CHIP_ID:
        return 0x200000;
    case CY_CC_4373_CHIP_ID:
        return 0x160000;
    default:
        brcmf_err("unknown chip: %s\n", ci->pub.name);
        break;
    }
    return 0;
}

static zx_status_t brcmf_chip_get_raminfo(struct brcmf_chip_priv* ci) {
    struct brcmf_core_priv* mem_core;
    struct brcmf_core* mem;

    mem = brcmf_chip_get_core(&ci->pub, CHIPSET_ARM_CR4_CORE);
    if (mem) {
        mem_core = container_of(mem, struct brcmf_core_priv, pub);
        ci->pub.ramsize = brcmf_chip_tcm_ramsize(mem_core);
        ci->pub.rambase = brcmf_chip_tcm_rambase(ci);
        if (!ci->pub.rambase) {
            brcmf_err("RAM base not provided with ARM CR4 core\n");
            return ZX_ERR_INVALID_ARGS;
        }
    } else {
        mem = brcmf_chip_get_core(&ci->pub, CHIPSET_SYS_MEM_CORE);
        if (mem) {
            mem_core = container_of(mem, struct brcmf_core_priv, pub);
            ci->pub.ramsize = brcmf_chip_sysmem_ramsize(mem_core);
            ci->pub.rambase = brcmf_chip_tcm_rambase(ci);
            if (!ci->pub.rambase) {
                brcmf_err("RAM base not provided with ARM CA7 core\n");
                return ZX_ERR_INVALID_ARGS;
            }
        } else {
            mem = brcmf_chip_get_core(&ci->pub, CHIPSET_INTERNAL_MEM_CORE);
            if (!mem) {
                brcmf_err("No memory cores found\n");
                return ZX_ERR_NO_MEMORY;
            }
            mem_core = container_of(mem, struct brcmf_core_priv, pub);
            brcmf_chip_socram_ramsize(mem_core, &ci->pub.ramsize, &ci->pub.srsize);
        }
    }
    brcmf_dbg(INFO, "RAM: base=0x%x size=%d (0x%x) sr=%d (0x%x)\n", ci->pub.rambase,
              ci->pub.ramsize, ci->pub.ramsize, ci->pub.srsize, ci->pub.srsize);

    if (!ci->pub.ramsize) {
        brcmf_err("RAM size is undetermined\n");
        return ZX_ERR_NO_MEMORY;
    }

    if (ci->pub.ramsize > BRCMF_CHIP_MAX_MEMSIZE) {
        brcmf_err("RAM size is incorrect\n");
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

static uint32_t brcmf_chip_dmp_get_desc(struct brcmf_chip_priv* ci, uint32_t* eromaddr,
                                        uint8_t* type) {
    uint32_t val;

    /* read next descriptor */
    val = ci->ops->read32(ci->ctx, *eromaddr);
    *eromaddr += 4;

    if (!type) {
        return val;
    }

    /* determine descriptor type */
    *type = (val & DMP_DESC_TYPE_MSK);
    if ((*type & ~DMP_DESC_ADDRSIZE_GT32) == DMP_DESC_ADDRESS) {
        *type = DMP_DESC_ADDRESS;
    }

    return val;
}

static zx_status_t brcmf_chip_dmp_get_regaddr(struct brcmf_chip_priv* ci, uint32_t* eromaddr,
                                              uint32_t* regbase, uint32_t* wrapbase) {
    uint8_t desc;
    uint32_t val;
    uint8_t mpnum = 0;
    uint8_t stype, sztype, wraptype;

    *regbase = 0;
    *wrapbase = 0;

    val = brcmf_chip_dmp_get_desc(ci, eromaddr, &desc);
    if (desc == DMP_DESC_MASTER_PORT) {
        mpnum = (val & DMP_MASTER_PORT_NUM) >> DMP_MASTER_PORT_NUM_S;
        wraptype = DMP_SLAVE_TYPE_MWRAP;
    } else if (desc == DMP_DESC_ADDRESS) {
        /* revert erom address */
        *eromaddr -= 4;
        wraptype = DMP_SLAVE_TYPE_SWRAP;
    } else {
        *eromaddr -= 4;
        return ZX_ERR_WRONG_TYPE;
    }

    do {
        /* locate address descriptor */
        do {
            val = brcmf_chip_dmp_get_desc(ci, eromaddr, &desc);
            /* unexpected table end */
            if (desc == DMP_DESC_EOT) {
                *eromaddr -= 4;
                return ZX_ERR_WRONG_TYPE;
            }
        } while (desc != DMP_DESC_ADDRESS && desc != DMP_DESC_COMPONENT);

        /* stop if we crossed current component border */
        if (desc == DMP_DESC_COMPONENT) {
            *eromaddr -= 4;
            return ZX_OK;
        }

        /* skip upper 32-bit address descriptor */
        if (val & DMP_DESC_ADDRSIZE_GT32) {
            brcmf_chip_dmp_get_desc(ci, eromaddr, NULL);
        }

        sztype = (val & DMP_SLAVE_SIZE_TYPE) >> DMP_SLAVE_SIZE_TYPE_S;

        /* next size descriptor can be skipped */
        if (sztype == DMP_SLAVE_SIZE_DESC) {
            val = brcmf_chip_dmp_get_desc(ci, eromaddr, NULL);
            /* skip upper size descriptor if present */
            if (val & DMP_DESC_ADDRSIZE_GT32) {
                brcmf_chip_dmp_get_desc(ci, eromaddr, NULL);
            }
        }

        /* only look for 4K register regions */
        if (sztype != DMP_SLAVE_SIZE_4K) {
            continue;
        }

        stype = (val & DMP_SLAVE_TYPE) >> DMP_SLAVE_TYPE_S;

        /* only regular slave and wrapper */
        if (*regbase == 0 && stype == DMP_SLAVE_TYPE_SLAVE) {
            *regbase = val & DMP_SLAVE_ADDR_BASE;
        }
        if (*wrapbase == 0 && stype == wraptype) {
            *wrapbase = val & DMP_SLAVE_ADDR_BASE;
        }
    } while (*regbase == 0 || *wrapbase == 0);

    return ZX_OK;
}

static zx_status_t brcmf_chip_dmp_erom_scan(struct brcmf_chip_priv* ci) {
    struct brcmf_core* core;
    uint32_t eromaddr;
    uint8_t desc_type = 0;
    uint32_t val;
    uint16_t id;
    uint8_t nmp, nsp, nmw, nsw, rev;
    uint32_t base, wrap;
    zx_status_t err;

    eromaddr = ci->ops->read32(ci->ctx, CORE_CC_REG(SI_ENUM_BASE, eromptr));

    while (desc_type != DMP_DESC_EOT) {
        val = brcmf_chip_dmp_get_desc(ci, &eromaddr, &desc_type);
        if (!(val & DMP_DESC_VALID)) {
            continue;
        }

        if (desc_type == DMP_DESC_EMPTY) {
            continue;
        }

        /* need a component descriptor */
        if (desc_type != DMP_DESC_COMPONENT) {
            continue;
        }

        id = (val & DMP_COMP_PARTNUM) >> DMP_COMP_PARTNUM_S;

        /* next descriptor must be component as well */
        val = brcmf_chip_dmp_get_desc(ci, &eromaddr, &desc_type);
        if (WARN_ON((val & DMP_DESC_TYPE_MSK) != DMP_DESC_COMPONENT)) {
            return ZX_ERR_WRONG_TYPE;
        }

        /* only look at cores with master port(s) */
        nmp = (val & DMP_COMP_NUM_MPORT) >> DMP_COMP_NUM_MPORT_S;
        nsp = (val & DMP_COMP_NUM_SPORT) >> DMP_COMP_NUM_SPORT_S;
        nmw = (val & DMP_COMP_NUM_MWRAP) >> DMP_COMP_NUM_MWRAP_S;
        nsw = (val & DMP_COMP_NUM_SWRAP) >> DMP_COMP_NUM_SWRAP_S;
        rev = (val & DMP_COMP_REVISION) >> DMP_COMP_REVISION_S;

        /* need core with ports */
        if (nmw + nsw == 0 && id != CHIPSET_PMU_CORE) {
            continue;
        }

        /* try to obtain register address info */
        err = brcmf_chip_dmp_get_regaddr(ci, &eromaddr, &base, &wrap);
        if (err != ZX_OK) {
            continue;
        }

        /* finally a core to be added */
        err = brcmf_chip_add_core(ci, id, base, wrap, &core);
        if (err != ZX_OK) {
            return err;
        }

        core->rev = rev;
    }

    return ZX_OK;
}

static zx_status_t brcmf_chip_recognition(struct brcmf_chip_priv* ci) {
    struct brcmf_core* core;
    uint32_t regdata;
    uint32_t socitype;
    zx_status_t ret;

    /* Get CC core rev
     * Chipid is assume to be at offset 0 from SI_ENUM_BASE
     * For different chiptypes or old sdio hosts w/o chipcommon,
     * other ways of recognition should be added here.
     */
    regdata = ci->ops->read32(ci->ctx, CORE_CC_REG(SI_ENUM_BASE, chipid));
    ci->pub.chip = regdata & CID_ID_MASK;
    ci->pub.chiprev = (regdata & CID_REV_MASK) >> CID_REV_SHIFT;
    socitype = (regdata & CID_TYPE_MASK) >> CID_TYPE_SHIFT;

    brcmf_chip_name(ci->pub.chip, ci->pub.name, sizeof(ci->pub.name));
    brcmf_dbg(INFO, "found %s chip: BCM%s, rev=%d\n", socitype == SOCI_SB ? "SB" : "AXI",
              ci->pub.name, ci->pub.chiprev);

    if (socitype == SOCI_SB) {
        if (ci->pub.chip != BRCM_CC_4329_CHIP_ID) {
            brcmf_err("SB chip is not supported\n");
            return ZX_ERR_WRONG_TYPE;
        }
        ci->iscoreup = brcmf_chip_sb_iscoreup;
        ci->coredisable = brcmf_chip_sb_coredisable;
        ci->resetcore = brcmf_chip_sb_resetcore;

        brcmf_chip_add_core(ci, CHIPSET_CHIPCOMMON_CORE, SI_ENUM_BASE, 0, &core);
        brcmf_chip_sb_corerev(ci, core);
        brcmf_chip_add_core(ci, CHIPSET_SDIO_DEV_CORE, BCM4329_CORE_BUS_BASE, 0, &core);
        brcmf_chip_sb_corerev(ci, core);
        brcmf_chip_add_core(ci, CHIPSET_INTERNAL_MEM_CORE, BCM4329_CORE_SOCRAM_BASE, 0, &core);
        brcmf_chip_sb_corerev(ci, core);
        brcmf_chip_add_core(ci, CHIPSET_ARM_CM3_CORE, BCM4329_CORE_ARM_BASE, 0, &core);
        brcmf_chip_sb_corerev(ci, core);

        brcmf_chip_add_core(ci, CHIPSET_80211_CORE, 0x18001000, 0, &core);
        brcmf_chip_sb_corerev(ci, core);
    } else if (socitype == SOCI_AI) {
        ci->iscoreup = brcmf_chip_ai_iscoreup;
        ci->coredisable = brcmf_chip_ai_coredisable;
        ci->resetcore = brcmf_chip_ai_resetcore;

        brcmf_dbg(TEMP, "about to erom_scan in SOCI_AI");
        brcmf_chip_dmp_erom_scan(ci);
        brcmf_dbg(TEMP, "Survived erom scan");
    } else {
        brcmf_err("chip backplane type %u is not supported\n", socitype);
        return ZX_ERR_WRONG_TYPE;
    }

    ret = brcmf_chip_cores_check(ci);
    brcmf_dbg(TEMP, "Survived cores_check");
    if (ret != ZX_OK) {
        return ret;
    }

    /* assure chip is passive for core access */
    brcmf_chip_set_passive(&ci->pub);
    brcmf_dbg(TEMP, "Survived set_passive");
    PAUSE;

    /* Call bus specific reset function now. Cores have been determined
     * but further access may require a chip specific reset at this point.
     */
    if (ci->ops->reset) {
        brcmf_dbg(TEMP, "Trying reset");

        ci->ops->reset(ci->ctx, &ci->pub);
        brcmf_dbg(TEMP, "Survived reset");
        PAUSE;
        brcmf_chip_set_passive(&ci->pub);
        brcmf_dbg(TEMP, "Survived passive");
        PAUSE;
    }

    //return brcmf_chip_get_raminfo(ci);
    ret = brcmf_chip_get_raminfo(ci);
    brcmf_dbg(TEMP, "chip_get_raminfo returned %d", ret);
    PAUSE;
    return ret;
}

static void brcmf_chip_disable_arm(struct brcmf_chip_priv* chip, uint16_t id) {
    struct brcmf_core* core;
    struct brcmf_core_priv* cpu;
    uint32_t val;

    core = brcmf_chip_get_core(&chip->pub, id);
    if (!core) {
        return;
    }

    switch (id) {
    case CHIPSET_ARM_CM3_CORE:
        brcmf_chip_coredisable(core, 0, 0);
        break;
    case CHIPSET_ARM_CR4_CORE:
    case CHIPSET_ARM_CA7_CORE:
        cpu = container_of(core, struct brcmf_core_priv, pub);

        /* clear all IOCTL bits except HALT bit */
        val = chip->ops->read32(chip->ctx, cpu->wrapbase + BC_CORE_CONTROL);
        val &= ARMCR4_BCMA_IOCTL_CPUHALT;
        brcmf_dbg(TEMP, "About to resetcore, id %d, val %d, CPUHALT", id, val);
        brcmf_chip_resetcore(core, val, ARMCR4_BCMA_IOCTL_CPUHALT, ARMCR4_BCMA_IOCTL_CPUHALT);
        break;
    default:
        brcmf_err("unknown id: %u\n", id);
        break;
    }
}

static int brcmf_chip_setup(struct brcmf_chip_priv* chip) {
    struct brcmf_chip* pub;
    struct brcmf_core_priv* cc;
    struct brcmf_core* pmu;
    uint32_t base;
    uint32_t val;
    int ret = 0;

    pub = &chip->pub;
    cc = list_first_entry(&chip->cores, struct brcmf_core_priv, list);
    base = cc->pub.base;

    /* get chipcommon capabilites */
    pub->cc_caps = chip->ops->read32(chip->ctx, CORE_CC_REG(base, capabilities));
    pub->cc_caps_ext = chip->ops->read32(chip->ctx, CORE_CC_REG(base, capabilities_ext));

    /* get pmu caps & rev */
    pmu = brcmf_chip_get_pmu(pub); /* after reading cc_caps_ext */
    if (pub->cc_caps & CC_CAP_PMU) {
        val = chip->ops->read32(chip->ctx, CORE_CC_REG(pmu->base, pmucapabilities));
        pub->pmurev = val & PCAP_REV_MASK;
        pub->pmucaps = val;
    }

    brcmf_dbg(INFO, "ccrev=%d, pmurev=%d, pmucaps=0x%x\n", cc->pub.rev, pub->pmurev, pub->pmucaps);

    /* execute bus core specific setup */
    if (chip->ops->setup) {
        ret = chip->ops->setup(chip->ctx, pub);
    }

    return ret;
}

zx_status_t brcmf_chip_attach(void* ctx, const struct brcmf_buscore_ops* ops,
                              struct brcmf_chip** chip_out) {
    struct brcmf_chip_priv* chip;
    zx_status_t err = ZX_OK;

    if (chip_out) {
        *chip_out = NULL;
    }

    if (WARN_ON(!ops->read32)) {
        err = ZX_ERR_INVALID_ARGS;
    }
    if (WARN_ON(!ops->write32)) {
        err = ZX_ERR_INVALID_ARGS;
    }
    if (WARN_ON(!ops->prepare)) {
        err = ZX_ERR_INVALID_ARGS;
    }
    if (WARN_ON(!ops->activate)) {
        err = ZX_ERR_INVALID_ARGS;
    }
    if (err != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
    }

    chip = calloc(1, sizeof(*chip));
    if (!chip) {
        return ZX_ERR_NO_MEMORY;
    }

    INIT_LIST_HEAD(&chip->cores);
    chip->num_cores = 0;
    chip->ops = ops;
    chip->ctx = ctx;

    err = ops->prepare(ctx);
    if (err != ZX_OK) {
        goto fail;
    }

    err = brcmf_chip_recognition(chip);
    brcmf_dbg(TEMP, "survived chip_recognition, err %s", zx_status_get_string(err));
    if (err != ZX_OK) {
        goto fail;
    }

    err = brcmf_chip_setup(chip);
    brcmf_dbg(TEMP, "survived chip_setup, err %s", zx_status_get_string(err));
    if (err != ZX_OK) {
        goto fail;
    }

    if (chip_out) {
        *chip_out = &chip->pub;
    }
    return ZX_OK;

fail:
    brcmf_chip_detach(&chip->pub);
    brcmf_dbg(TEMP, "survived fail-detach");
    return err;
}

void brcmf_chip_detach(struct brcmf_chip* pub) {
    struct brcmf_chip_priv* chip;
    //struct brcmf_core_priv* core;
    //struct brcmf_core_priv* tmp; //unused

    chip = container_of(pub, struct brcmf_chip_priv, pub);
    brcmf_err("Need to free the core list!!!"); // TODO(cphoenix): Re-enable this code ASAP.
/*    list_for_each_entry_safe(core, tmp, &chip->cores, list) {
        list_del(&core->list);
        free(core);
    }*/
    free(chip);
}

struct brcmf_core* brcmf_chip_get_core(struct brcmf_chip* pub, uint16_t coreid) {
    struct brcmf_chip_priv* chip;
    struct brcmf_core_priv* core;

    chip = container_of(pub, struct brcmf_chip_priv, pub);
    list_for_each_entry(core, &chip->cores, list) {
        if (core->pub.id == coreid) {
            return &core->pub;
        }
    }

    return NULL;
}

struct brcmf_core* brcmf_chip_get_chipcommon(struct brcmf_chip* pub) {
    struct brcmf_chip_priv* chip;
    struct brcmf_core_priv* cc;

    chip = container_of(pub, struct brcmf_chip_priv, pub);
    cc = list_first_entry(&chip->cores, struct brcmf_core_priv, list);
    if (WARN_ON(!cc || cc->pub.id != CHIPSET_CHIPCOMMON_CORE)) {
        return brcmf_chip_get_core(pub, CHIPSET_CHIPCOMMON_CORE);
    }
    return &cc->pub;
}

struct brcmf_core* brcmf_chip_get_pmu(struct brcmf_chip* pub) {
    struct brcmf_core* cc = brcmf_chip_get_chipcommon(pub);
    struct brcmf_core* pmu;

    /* See if there is separated PMU core available */
    if (cc->rev >= 35 && pub->cc_caps_ext & BC_CORE_ASYNC_BACKOFF_CAPABILITY_PRESENT) {
        pmu = brcmf_chip_get_core(pub, CHIPSET_PMU_CORE);
        if (pmu) {
            return pmu;
        }
    }

    /* Fallback to ChipCommon core for older hardware */
    return cc;
}

bool brcmf_chip_iscoreup(struct brcmf_core* pub) {
    struct brcmf_core_priv* core;

    core = container_of(pub, struct brcmf_core_priv, pub);
    return core->chip->iscoreup(core);
}

void brcmf_chip_coredisable(struct brcmf_core* pub, uint32_t prereset, uint32_t reset) {
    struct brcmf_core_priv* core;

    core = container_of(pub, struct brcmf_core_priv, pub);
    core->chip->coredisable(core, prereset, reset);
}

void brcmf_chip_resetcore(struct brcmf_core* pub, uint32_t prereset, uint32_t reset,
                          uint32_t postreset) {
    struct brcmf_core_priv* core;

    core = container_of(pub, struct brcmf_core_priv, pub);
    core->chip->resetcore(core, prereset, reset, postreset);
}

static void brcmf_chip_cm3_set_passive(struct brcmf_chip_priv* chip) {
    struct brcmf_core* core;
    struct brcmf_core_priv* sr;

    brcmf_dbg(TEMP, "cm3");
    brcmf_chip_disable_arm(chip, CHIPSET_ARM_CM3_CORE);
    core = brcmf_chip_get_core(&chip->pub, CHIPSET_80211_CORE);
    brcmf_dbg(TEMP, "cm3->resetcore");
    brcmf_chip_resetcore(core, D11_BCMA_IOCTL_PHYRESET | D11_BCMA_IOCTL_PHYCLOCKEN,
                         D11_BCMA_IOCTL_PHYCLOCKEN, D11_BCMA_IOCTL_PHYCLOCKEN);
    brcmf_dbg(TEMP, "cm3<-resetcore->get_core");
    core = brcmf_chip_get_core(&chip->pub, CHIPSET_INTERNAL_MEM_CORE);
    brcmf_dbg(TEMP, "get_core->reset");
    brcmf_chip_resetcore(core, 0, 0, 0);

    /* disable bank #3 remap for this device */
    if (chip->pub.chip == BRCM_CC_43430_CHIP_ID) {
        brcmf_dbg(TEMP, "cm3 43430");
        sr = container_of(core, struct brcmf_core_priv, pub);
        brcmf_chip_core_write32(sr, SOCRAMREGOFFS(bankidx), 3);
        brcmf_chip_core_write32(sr, SOCRAMREGOFFS(bankpda), 0);
    }
    brcmf_dbg(TEMP, "cm3 survived");
}

static bool brcmf_chip_cm3_set_active(struct brcmf_chip_priv* chip) {
    struct brcmf_core* core;

    core = brcmf_chip_get_core(&chip->pub, CHIPSET_INTERNAL_MEM_CORE);
    if (!brcmf_chip_iscoreup(core)) {
        brcmf_err("SOCRAM core is down after reset?\n");
        return false;
    }

    chip->ops->activate(chip->ctx, &chip->pub, 0);

    core = brcmf_chip_get_core(&chip->pub, CHIPSET_ARM_CM3_CORE);
    brcmf_chip_resetcore(core, 0, 0, 0);

    return true;
}

static inline void brcmf_chip_cr4_set_passive(struct brcmf_chip_priv* chip) {
    struct brcmf_core* core;
    brcmf_dbg(TEMP, "1");

    brcmf_chip_disable_arm(chip, CHIPSET_ARM_CR4_CORE);
    brcmf_dbg(TEMP, "2");

    core = brcmf_chip_get_core(&chip->pub, CHIPSET_80211_CORE);
    brcmf_dbg(TEMP, "resetcore, id %d, val %d, PHYCLOCKEN", CHIPSET_80211_CORE,
              D11_BCMA_IOCTL_PHYRESET | D11_BCMA_IOCTL_PHYCLOCKEN);
    PAUSE;
    brcmf_chip_resetcore(core, D11_BCMA_IOCTL_PHYRESET | D11_BCMA_IOCTL_PHYCLOCKEN,
                         D11_BCMA_IOCTL_PHYCLOCKEN, D11_BCMA_IOCTL_PHYCLOCKEN);
    brcmf_dbg(TEMP, "4");
    PAUSE;
}

static bool brcmf_chip_cr4_set_active(struct brcmf_chip_priv* chip, uint32_t rstvec) {
    struct brcmf_core* core;

    chip->ops->activate(chip->ctx, &chip->pub, rstvec);

    /* restore ARM */
    core = brcmf_chip_get_core(&chip->pub, CHIPSET_ARM_CR4_CORE);
    brcmf_chip_resetcore(core, ARMCR4_BCMA_IOCTL_CPUHALT, 0, 0);

    return true;
}

static inline void brcmf_chip_ca7_set_passive(struct brcmf_chip_priv* chip) {
    struct brcmf_core* core;

    brcmf_chip_disable_arm(chip, CHIPSET_ARM_CA7_CORE);

    core = brcmf_chip_get_core(&chip->pub, CHIPSET_80211_CORE);
    brcmf_chip_resetcore(core, D11_BCMA_IOCTL_PHYRESET | D11_BCMA_IOCTL_PHYCLOCKEN,
                         D11_BCMA_IOCTL_PHYCLOCKEN, D11_BCMA_IOCTL_PHYCLOCKEN);
}

static bool brcmf_chip_ca7_set_active(struct brcmf_chip_priv* chip, uint32_t rstvec) {
    struct brcmf_core* core;

    chip->ops->activate(chip->ctx, &chip->pub, rstvec);

    /* restore ARM */
    core = brcmf_chip_get_core(&chip->pub, CHIPSET_ARM_CA7_CORE);
    brcmf_chip_resetcore(core, ARMCR4_BCMA_IOCTL_CPUHALT, 0, 0);

    return true;
}

void brcmf_chip_set_passive(struct brcmf_chip* pub) {
    struct brcmf_chip_priv* chip;
    struct brcmf_core* arm;

    brcmf_dbg(TEMP, "Enter");

    chip = container_of(pub, struct brcmf_chip_priv, pub);
    arm = brcmf_chip_get_core(pub, CHIPSET_ARM_CR4_CORE);
    brcmf_dbg(TEMP, "cr4 arm %p", arm);
    if (arm) {
        brcmf_chip_cr4_set_passive(chip);
        return;
    }
    arm = brcmf_chip_get_core(pub, CHIPSET_ARM_CA7_CORE);
    brcmf_dbg(TEMP, "ca7 arm %p", arm);
    if (arm) {
        brcmf_chip_ca7_set_passive(chip);
        return;
    }
    arm = brcmf_chip_get_core(pub, CHIPSET_ARM_CM3_CORE);
    brcmf_dbg(TEMP, "cm3 arm %p", arm);
    if (arm) {
        brcmf_chip_cm3_set_passive(chip);
        brcmf_dbg(TEMP, "Survived cm3_set_passive");
        return;
    }
}

bool brcmf_chip_set_active(struct brcmf_chip* pub, uint32_t rstvec) {
    struct brcmf_chip_priv* chip;
    struct brcmf_core* arm;

    brcmf_dbg(TRACE, "Enter\n");

    chip = container_of(pub, struct brcmf_chip_priv, pub);
    arm = brcmf_chip_get_core(pub, CHIPSET_ARM_CR4_CORE);
    if (arm) {
        return brcmf_chip_cr4_set_active(chip, rstvec);
    }
    arm = brcmf_chip_get_core(pub, CHIPSET_ARM_CA7_CORE);
    if (arm) {
        return brcmf_chip_ca7_set_active(chip, rstvec);
    }
    arm = brcmf_chip_get_core(pub, CHIPSET_ARM_CM3_CORE);
    if (arm) {
        return brcmf_chip_cm3_set_active(chip);
    }

    return false;
}

bool brcmf_chip_sr_capable(struct brcmf_chip* pub) {
    uint32_t base, addr, reg;
    uint32_t pmu_cc3_mask = ~0;
    struct brcmf_chip_priv* chip;
    struct brcmf_core* pmu = brcmf_chip_get_pmu(pub);

    brcmf_dbg(TRACE, "Enter\n");

    /* old chips with PMU version less than 17 don't support save restore */
    if (pub->pmurev < 17) {
        return false;
    }

    base = brcmf_chip_get_chipcommon(pub)->base;
    chip = container_of(pub, struct brcmf_chip_priv, pub);

    switch (pub->chip) {
    case BRCM_CC_4354_CHIP_ID:
    case BRCM_CC_4356_CHIP_ID:
    case BRCM_CC_4345_CHIP_ID:
        /* explicitly check SR engine enable bit */
        pmu_cc3_mask = BIT(2);
        /* fall-through */
    case BRCM_CC_43241_CHIP_ID:
    case BRCM_CC_4335_CHIP_ID:
    case BRCM_CC_4339_CHIP_ID:
        /* read PMU chipcontrol register 3 */
        addr = CORE_CC_REG(pmu->base, chipcontrol_addr);
        chip->ops->write32(chip->ctx, addr, 3);
        addr = CORE_CC_REG(pmu->base, chipcontrol_data);
        reg = chip->ops->read32(chip->ctx, addr);
        return (reg & pmu_cc3_mask) != 0;
    case BRCM_CC_43430_CHIP_ID:
        addr = CORE_CC_REG(base, sr_control1);
        reg = chip->ops->read32(chip->ctx, addr);
        return reg != 0;
    default:
        addr = CORE_CC_REG(pmu->base, pmucapabilities_ext);
        reg = chip->ops->read32(chip->ctx, addr);
        if ((reg & PCAPEXT_SR_SUPPORTED_MASK) == 0) {
            return false;
        }

        addr = CORE_CC_REG(pmu->base, retention_ctl);
        reg = chip->ops->read32(chip->ctx, addr);
        return (reg & (PMU_RCTL_MACPHY_DISABLE_MASK | PMU_RCTL_LOGIC_DISABLE_MASK)) == 0;
    }
}
