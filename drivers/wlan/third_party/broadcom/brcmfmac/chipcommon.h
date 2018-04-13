/*
 * Copyright (c) 2010 Broadcom Corporation
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

#ifndef _SBCHIPC_H
#define _SBCHIPC_H

#include "defs.h" /* for PAD macro */

#define CHIPCREGOFFS(field) offsetof(struct chipcregs, field)

struct chipcregs {
    uint32_t chipid; /* 0x0 */
    uint32_t capabilities;
    uint32_t corecontrol; /* corerev >= 1 */
    uint32_t bist;

    /* OTP */
    uint32_t otpstatus; /* 0x10, corerev >= 10 */
    uint32_t otpcontrol;
    uint32_t otpprog;
    uint32_t otplayout; /* corerev >= 23 */

    /* Interrupt control */
    uint32_t intstatus; /* 0x20 */
    uint32_t intmask;

    /* Chip specific regs */
    uint32_t chipcontrol; /* 0x28, rev >= 11 */
    uint32_t chipstatus;  /* 0x2c, rev >= 11 */

    /* Jtag Master */
    uint32_t jtagcmd; /* 0x30, rev >= 10 */
    uint32_t jtagir;
    uint32_t jtagdr;
    uint32_t jtagctrl;

    /* serial flash interface registers */
    uint32_t flashcontrol; /* 0x40 */
    uint32_t flashaddress;
    uint32_t flashdata;
    uint32_t PAD[1];

    /* Silicon backplane configuration broadcast control */
    uint32_t broadcastaddress; /* 0x50 */
    uint32_t broadcastdata;

    /* gpio - cleared only by power-on-reset */
    uint32_t gpiopullup;      /* 0x58, corerev >= 20 */
    uint32_t gpiopulldown;    /* 0x5c, corerev >= 20 */
    uint32_t gpioin;          /* 0x60 */
    uint32_t gpioout;         /* 0x64 */
    uint32_t gpioouten;       /* 0x68 */
    uint32_t gpiocontrol;     /* 0x6C */
    uint32_t gpiointpolarity; /* 0x70 */
    uint32_t gpiointmask;     /* 0x74 */

    /* GPIO events corerev >= 11 */
    uint32_t gpioevent;
    uint32_t gpioeventintmask;

    /* Watchdog timer */
    uint32_t watchdog; /* 0x80 */

    /* GPIO events corerev >= 11 */
    uint32_t gpioeventintpolarity;

    /* GPIO based LED powersave registers corerev >= 16 */
    uint32_t gpiotimerval; /* 0x88 */
    uint32_t gpiotimeroutmask;

    /* clock control */
    uint32_t clockcontrol_n;   /* 0x90 */
    uint32_t clockcontrol_sb;  /* aka m0 */
    uint32_t clockcontrol_pci; /* aka m1 */
    uint32_t clockcontrol_m2;  /* mii/uart/mipsref */
    uint32_t clockcontrol_m3;  /* cpu */
    uint32_t clkdiv;           /* corerev >= 3 */
    uint32_t gpiodebugsel;     /* corerev >= 28 */
    uint32_t capabilities_ext; /* 0xac  */

    /* pll delay registers (corerev >= 4) */
    uint32_t pll_on_delay; /* 0xb0 */
    uint32_t fref_sel_delay;
    uint32_t slow_clk_ctl; /* 5 < corerev < 10 */
    uint32_t PAD;

    /* Instaclock registers (corerev >= 10) */
    uint32_t system_clk_ctl; /* 0xc0 */
    uint32_t clkstatestretch;
    uint32_t PAD[2];

    /* Indirect backplane access (corerev >= 22) */
    uint32_t bp_addrlow; /* 0xd0 */
    uint32_t bp_addrhigh;
    uint32_t bp_data;
    uint32_t PAD;
    uint32_t bp_indaccess;
    uint32_t PAD[3];

    /* More clock dividers (corerev >= 32) */
    uint32_t clkdiv2;
    uint32_t PAD[2];

    /* In AI chips, pointer to erom */
    uint32_t eromptr; /* 0xfc */

    /* ExtBus control registers (corerev >= 3) */
    uint32_t pcmcia_config; /* 0x100 */
    uint32_t pcmcia_memwait;
    uint32_t pcmcia_attrwait;
    uint32_t pcmcia_iowait;
    uint32_t ide_config;
    uint32_t ide_memwait;
    uint32_t ide_attrwait;
    uint32_t ide_iowait;
    uint32_t prog_config;
    uint32_t prog_waitcount;
    uint32_t flash_config;
    uint32_t flash_waitcount;
    uint32_t SECI_config; /* 0x130 SECI configuration */
    uint32_t PAD[3];

    /* Enhanced Coexistence Interface (ECI) registers (corerev >= 21) */
    uint32_t eci_output; /* 0x140 */
    uint32_t eci_control;
    uint32_t eci_inputlo;
    uint32_t eci_inputmi;
    uint32_t eci_inputhi;
    uint32_t eci_inputintpolaritylo;
    uint32_t eci_inputintpolaritymi;
    uint32_t eci_inputintpolarityhi;
    uint32_t eci_intmasklo;
    uint32_t eci_intmaskmi;
    uint32_t eci_intmaskhi;
    uint32_t eci_eventlo;
    uint32_t eci_eventmi;
    uint32_t eci_eventhi;
    uint32_t eci_eventmasklo;
    uint32_t eci_eventmaskmi;
    uint32_t eci_eventmaskhi;
    uint32_t PAD[3];

    /* SROM interface (corerev >= 32) */
    uint32_t sromcontrol; /* 0x190 */
    uint32_t sromaddress;
    uint32_t sromdata;
    uint32_t PAD[17];

    /* Clock control and hardware workarounds (corerev >= 20) */
    uint32_t clk_ctl_st; /* 0x1e0 */
    uint32_t hw_war;
    uint32_t PAD[70];

    /* UARTs */
    uint8_t uart0data; /* 0x300 */
    uint8_t uart0imr;
    uint8_t uart0fcr;
    uint8_t uart0lcr;
    uint8_t uart0mcr;
    uint8_t uart0lsr;
    uint8_t uart0msr;
    uint8_t uart0scratch;
    uint8_t PAD[248]; /* corerev >= 1 */

    uint8_t uart1data; /* 0x400 */
    uint8_t uart1imr;
    uint8_t uart1fcr;
    uint8_t uart1lcr;
    uint8_t uart1mcr;
    uint8_t uart1lsr;
    uint8_t uart1msr;
    uint8_t uart1scratch;
    uint32_t PAD[62];

    /* save/restore, corerev >= 48 */
    uint32_t sr_capability; /* 0x500 */
    uint32_t sr_control0;   /* 0x504 */
    uint32_t sr_control1;   /* 0x508 */
    uint32_t gpio_control;  /* 0x50C */
    uint32_t PAD[60];

    /* PMU registers (corerev >= 20) */
    uint32_t pmucontrol; /* 0x600 */
    uint32_t pmucapabilities;
    uint32_t pmustatus;
    uint32_t res_state;
    uint32_t res_pending;
    uint32_t pmutimer;
    uint32_t min_res_mask;
    uint32_t max_res_mask;
    uint32_t res_table_sel;
    uint32_t res_dep_mask;
    uint32_t res_updn_timer;
    uint32_t res_timer;
    uint32_t clkstretch;
    uint32_t pmuwatchdog;
    uint32_t gpiosel;    /* 0x638, rev >= 1 */
    uint32_t gpioenable; /* 0x63c, rev >= 1 */
    uint32_t res_req_timer_sel;
    uint32_t res_req_timer;
    uint32_t res_req_mask;
    uint32_t pmucapabilities_ext; /* 0x64c, pmurev >=15 */
    uint32_t chipcontrol_addr;    /* 0x650 */
    uint32_t chipcontrol_data;    /* 0x654 */
    uint32_t regcontrol_addr;
    uint32_t regcontrol_data;
    uint32_t pllcontrol_addr;
    uint32_t pllcontrol_data;
    uint32_t pmustrapopt;   /* 0x668, corerev >= 28 */
    uint32_t pmu_xtalfreq;  /* 0x66C, pmurev >= 10 */
    uint32_t retention_ctl; /* 0x670, pmurev >= 15 */
    uint32_t PAD[3];
    uint32_t retention_grpidx; /* 0x680 */
    uint32_t retention_grpctl; /* 0x684 */
    uint32_t PAD[94];
    uint16_t sromotp[768];
};

// clang-format off

/* chipid */
#define CID_ID_MASK    0x0000ffff  /* Chip Id mask */
#define CID_REV_MASK   0x000f0000 /* Chip Revision mask */
#define CID_REV_SHIFT  16        /* Chip Revision shift */
#define CID_PKG_MASK   0x00f00000 /* Package Option mask */
#define CID_PKG_SHIFT  20        /* Package Option shift */
#define CID_CC_MASK    0x0f000000  /* CoreCount (corerev >= 4) */
#define CID_CC_SHIFT   24
#define CID_TYPE_MASK  0xf0000000 /* Chip Type */
#define CID_TYPE_SHIFT 28

/* capabilities */
#define CC_CAP_UARTS_MASK  0x00000003 /* Number of UARTs */
#define CC_CAP_MIPSEB      0x00000004     /* MIPS is in big-endian mode */
#define CC_CAP_UCLKSEL     0x00000018    /* UARTs clock select */
/* UARTs are driven by internal divided clock */
#define CC_CAP_UINTCLK     0x00000008
#define CC_CAP_UARTGPIO    0x00000020    /* UARTs own GPIOs 15:12 */
#define CC_CAP_EXTBUS_MASK 0x000000c0 /* External bus mask */
#define CC_CAP_EXTBUS_NONE 0x00000000 /* No ExtBus present */
#define CC_CAP_EXTBUS_FULL 0x00000040 /* ExtBus: PCMCIA, IDE & Prog */
#define CC_CAP_EXTBUS_PROG 0x00000080 /* ExtBus: ProgIf only */
#define CC_CAP_FLASH_MASK  0x00000700  /* Type of flash */
#define CC_CAP_PLL_MASK    0x00038000    /* Type of PLL */
#define CC_CAP_PWR_CTL     0x00040000     /* Power control */
#define CC_CAP_OTPSIZE     0x00380000     /* OTP Size (0 = none) */
#define CC_CAP_OTPSIZE_SHIFT 19       /* OTP Size shift */
#define CC_CAP_OTPSIZE_BASE 5         /* OTP Size base */
#define CC_CAP_JTAGP       0x00400000       /* JTAG Master Present */
#define CC_CAP_ROM         0x00800000         /* Internal boot rom active */
#define CC_CAP_BKPLN64     0x08000000     /* 64-bit backplane */
#define CC_CAP_PMU         0x10000000         /* PMU Present, rev >= 20 */
#define CC_CAP_SROM        0x40000000        /* Srom Present, rev >= 32 */
/* Nand flash present, rev >= 35 */
#define CC_CAP_NFLASH      0x80000000

#define CC_CAP2_SECI 0x00000001 /* SECI Present, rev >= 36 */
/* GSIO (spi/i2c) present, rev >= 37 */
#define CC_CAP2_GSIO 0x00000002

/* pmucapabilities */
#define PCAP_REV_MASK   0x000000ff
#define PCAP_RC_MASK    0x00001f00
#define PCAP_RC_SHIFT   8
#define PCAP_TC_MASK    0x0001e000
#define PCAP_TC_SHIFT   13
#define PCAP_PC_MASK    0x001e0000
#define PCAP_PC_SHIFT   17
#define PCAP_VC_MASK    0x01e00000
#define PCAP_VC_SHIFT   21
#define PCAP_CC_MASK    0x1e000000
#define PCAP_CC_SHIFT   25
#define PCAP5_PC_MASK   0x003e0000 /* PMU corerev >= 5 */
#define PCAP5_PC_SHIFT  17
#define PCAP5_VC_MASK   0x07c00000
#define PCAP5_VC_SHIFT  22
#define PCAP5_CC_MASK   0xf8000000
#define PCAP5_CC_SHIFT  27
/* pmucapabilites_ext PMU rev >= 15 */
#define PCAPEXT_SR_SUPPORTED_MASK    (1 << 1)
/* retention_ctl PMU rev >= 15 */
#define PMU_RCTL_MACPHY_DISABLE_MASK (1 << 26)
#define PMU_RCTL_LOGIC_DISABLE_MASK  (1 << 27)

// clang-format on

/*
 * Maximum delay for the PMU state transition in us.
 * This is an upper bound intended for spinwaits etc.
 */
#define PMU_MAX_TRANSITION_DLY_USEC 15000

#endif /* _SBCHIPC_H */
