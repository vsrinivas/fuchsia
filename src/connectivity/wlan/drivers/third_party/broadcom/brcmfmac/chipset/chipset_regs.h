// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CHIPSET_REGS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CHIPSET_REGS_H_

#include <zircon/types.h>

namespace wlan {
namespace brcmfmac {

// Enumeration space base.
constexpr uint32_t SI_ENUM_BASE = 0x18000000;

enum class CommonCoreId : uint16_t {
  kInvalid = 0,
  kBrcm43143 = 43143,
  kBrcm43235 = 43235,
  kBrcm43236 = 43236,
  kBrcm43238 = 43238,
  kBrcm43241 = 0x4324,
  kBrcm43242 = 43242,
  kBrcm4329 = 0x4329,
  kBrcm4330 = 0x4330,
  kBrcm4334 = 0x4334,
  kBrcm43340 = 43340,
  kBrcm43341 = 43341,
  kBrcm43362 = 43362,
  kBrcm4335 = 0x4335,
  kBrcm4339 = 0x4339,
  kBrcm43430 = 43430,
  kBrcm4345 = 0x4345,
  kBrcm43465 = 43465,
  kBrcm4350 = 0x4350,
  kBrcm43525 = 43525,
  kBrcm4354 = 0x4354,
  kBrcm4356 = 0x4356,
  kBrcm4359 = 0x4359,
  kBrcm43566 = 43566,
  kBrcm43567 = 43567,
  kBrcm43569 = 43569,
  kBrcm43570 = 43570,
  kBrcm4358 = 0x4358,
  kBrcm43602 = 43602,
  kBrcm4365 = 0x4365,
  kBrcm4366 = 0x4366,
  kBrcm4371 = 0x4371,
  kCypress4373 = 0x4373,
};

struct [[gnu::packed]] ChipsetCoreRegs {
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
  uint32_t pad0;

  /* Instaclock registers (corerev >= 10) */
  uint32_t system_clk_ctl; /* 0xc0 */
  uint32_t clkstatestretch;
  uint32_t pad1[2];

  /* Indirect backplane access (corerev >= 22) */
  uint32_t bp_addrlow; /* 0xd0 */
  uint32_t bp_addrhigh;
  uint32_t bp_data;
  uint32_t pad2;
  uint32_t bp_indaccess;
  uint32_t pad3[3];

  /* More clock dividers (corerev >= 32) */
  uint32_t clkdiv2;
  uint32_t pad4[2];

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
  uint32_t pad5[3];

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
  uint32_t pad6[3];

  /* SROM interface (corerev >= 32) */
  uint32_t sromcontrol; /* 0x190 */
  uint32_t sromaddress;
  uint32_t sromdata;
  uint32_t pad7[17];

  /* Clock control and hardware workarounds (corerev >= 20) */
  uint32_t clk_ctl_st; /* 0x1e0 */
  uint32_t hw_war;
  uint32_t pad8[70];

  /* UARTs */
  uint8_t uart0data; /* 0x300 */
  uint8_t uart0imr;
  uint8_t uart0fcr;
  uint8_t uart0lcr;
  uint8_t uart0mcr;
  uint8_t uart0lsr;
  uint8_t uart0msr;
  uint8_t uart0scratch;
  uint8_t pad9[248]; /* corerev >= 1 */

  uint8_t uart1data; /* 0x400 */
  uint8_t uart1imr;
  uint8_t uart1fcr;
  uint8_t uart1lcr;
  uint8_t uart1mcr;
  uint8_t uart1lsr;
  uint8_t uart1msr;
  uint8_t uart1scratch;
  uint32_t pad10[62];

  /* save/restore, corerev >= 48 */
  uint32_t sr_capability; /* 0x500 */
  uint32_t sr_control0;   /* 0x504 */
  uint32_t sr_control1;   /* 0x508 */
  uint32_t gpio_control;  /* 0x50C */
  uint32_t pad11[60];

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
  uint32_t pad12[3];
  uint32_t retention_grpidx; /* 0x680 */
  uint32_t retention_grpctl; /* 0x684 */
  uint32_t pad13[94];
  uint16_t sromotp[768];
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CHIPSET_REGS_H_
