// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <hw/reg.h>

#define DWC3_READ32(a)      readl(a)
#define DWC3_WRITE32(a, v)  writel(v, a)
#define DWC3_READ64(a)      readll(a)
#define DWC3_WRITE64(a, v)  writell(v, a)

#define DWC3_MASK(start, count) (((1 << (count)) - 1) << (start))
#define DWC3_GET_BITS32(src, start, count) ((DWC3_READ32(src) & DWC3_MASK(start, count)) >> (start))
#define DWC3_SET_BITS32(dest, start, count, value) \
            DWC3_WRITE32(dest, (DWC3_READ32(dest) & ~DWC3_MASK(start, count)) | \
                                (((value) << (start)) & DWC3_MASK(start, count)))

// XHCI register offsets
#define CAPLENGTH           0x0000              // Host Controller Operational Registers
#define CAPLENGTH_HCIVERSION_START  16
#define CAPLENGTH_HCIVERSION_BITS   16
#define CAPLENGTH_CAPLENGTH_START   0
#define CAPLENGTH_CAPLENGTH_BITS    8

#define HCSPARAMS1          0x0004              // Structural Parameters 1 Register
#define HCSPARAMS1_MAXPORTS_START   24
#define HCSPARAMS1_MAXPORTS_BITS    8
#define HCSPARAMS1_MAXINTRS_START   8
#define HCSPARAMS1_MAXINTRS_BITS    11
#define HCSPARAMS1_MAXSLOTS_START   0
#define HCSPARAMS1_MAXSLOTS_BITS    8

#define HCSPARAMS2          0x0008              // Structural Parameters 2 Register
#define HCSPARAMS2_MAXSCRATCHPADBUFS_START      27
#define HCSPARAMS2_MAXSCRATCHPADBUFS_BITS       5
#define HCSPARAMS2_SPR                          (1 << 26)
#define HCSPARAMS2_MAXSCRATCHPADBUFS_HI_START   21
#define HCSPARAMS2_MAXSCRATCHPADBUFS_HI_BITS    5
#define HCSPARAMS2_ERSTMAX_START                4
#define HCSPARAMS2_ERSTMAX_BITS                 4
#define HCSPARAMS2_IST_START                    0
#define HCSPARAMS2_IST_BITS                     4

#define HCSPARAMS3          0x000c              // Structural Parameters 3 Register
#define HCSPARAMS3_U2_DEVICE_EXIT_LAT_START     16
#define HCSPARAMS3_U2_DEVICE_EXIT_LAT_BITS      16
#define HCSPARAMS3_U1_DEVICE_EXIT_LAT_START     0
#define HCSPARAMS3_U1_DEVICE_EXIT_LAT_BITS      8

#define HCCPARAMS1          0x0010              // Capability Parameters 1 Register
#define HCCPARAMS1_XECP_START           16
#define HCCPARAMS1_XECP_BITS            16
#define HCCPARAMS1_MAXPSASIZE_START     12
#define HCCPARAMS1_MAXPSASIZE_BITS      4
#define HCCPARAMS1_CFC                  (1 << 11)
#define HCCPARAMS1_SEC                  (1 << 10)
#define HCCPARAMS1_SPC                  (1 << 9)
#define HCCPARAMS1_PAE                  (1 << 8)
#define HCCPARAMS1_NSS                  (1 << 7)
#define HCCPARAMS1_LTC                  (1 << 6)
#define HCCPARAMS1_LHRC                 (1 << 5)
#define HCCPARAMS1_PIND                 (1 << 4)
#define HCCPARAMS1_PPC                  (1 << 3)
#define HCCPARAMS1_CSZ                  (1 << 2)
#define HCCPARAMS1_BNC                  (1 << 1)
#define HCCPARAMS1_AC64                 (1 << 0)

#define DBOFF               0x0014              // Doorbell Offset Register
#define RTSOFF              0x0018              // Runtime Register Space Offset Register

#define HCCPARAMS2          0x001c              // Host Controller Capability Parameters 2
#define HCCPARAMS2_ETC      (1 << 6)
#define HCCPARAMS2_CIC      (1 << 5)
#define HCCPARAMS2_LEC      (1 << 4)
#define HCCPARAMS2_CTC      (1 << 3)
#define HCCPARAMS2_FSC      (1 << 2)
#define HCCPARAMS2_CMC      (1 << 1)
#define HCCPARAMS2_U3C      (1 << 0)

// Global register offsets
#define GSBUSCFG0           0xc100              // Global SoC Bus Configuration Register 0
#define GSBUSCFG1           0xc104              // Global SoC Bus Configuration Register 1
#define GTXTHRCFG           0xc108              // Global Tx Threshold Control Register
#define GRXTHRCFG           0xc10c              // Global Rx Threshold Control Register

#define GCTL                0xc110              // Global Core Control Register
#define GCTL_PWRDNSCALE(n)      (((n) & 0x1fff) << 19)
#define GCTL_PWRDNSCALE_START   19
#define GCTL_PWRDNSCALE_BITS    13
#define GCTL_MASTERFILTBYPASS   (1 << 18)
#define GCTL_BYPSSETADDR        (1 << 17)
#define GCTL_U2RSTECN           (1 << 16)
#define GCTL_FRMSCLDWN_START    14
#define GCTL_FRMSCLDWN_BITS     2
#define GCTL_PRTCAPDIR_START    12
#define GCTL_PRTCAPDIR_BITS     2
#define GCTL_PRTCAPDIR_HOST     (1 << GCTL_PRTCAPDIR_START)
#define GCTL_PRTCAPDIR_DEVICE   (2 << GCTL_PRTCAPDIR_START)
#define GCTL_PRTCAPDIR_OTG      (3 << GCTL_PRTCAPDIR_START)
#define GCTL_PRTCAPDIR_MASK     (3 << GCTL_PRTCAPDIR_START)
#define GCTL_CORESOFTRESET      (1 << 11)
#define GCTL_U1_U2_TIMER_SCALE  (1 << 9)
#define GCTL_DEBUGATTACH        (1 << 8)
#define GCTL_SCALEDOWN_START    4
#define GCTL_SCALEDOWN_BITS     2
#define GCTL_DISSCRAMBLE        (1 << 3)
#define GCTL_U2EXIT_LFPS        (1 << 2)
#define GCTL_GBL_HIBERNATION_EN (1 << 1)
#define GCTL_DSBLCLKGTNG        (1 << 0)

#define GPMSTS                  0xc114              // Global Power Management Status Register
#define GSTS                    0xc118              // Global Status Register
#define GSTS_CBELT_START        18
#define GSTS_CBELT_BITS         4
#define GSTS_CBELT(s)           (((s) >> GSTS_CBELT_START) & ((1 << GSTS_CBELT_START) - 1))
#define GSTS_SSIC_IP            (1 << 11)
#define GSTS_OTG_IP             (1 << 10)
#define GSTS_BC_IP              (1 << 9)
#define GSTS_ADP_IP             (1 << 8)
#define GSTS_HOST_IP            (1 << 7)
#define GSTS_DEVICE_IP          (1 << 6)
#define GSTS_CSR_TIMEOUT        (1 << 5)
#define GSTS_BUSERRADDRVLD      (1 << 4)
#define GSTS_CURMOD_START       0
#define GSTS_CURMOD_BITS        2
#define GSTS_CURMOD(s)         (((s) >> GSTS_CURMOD_START) & ((1 << GSTS_CURMOD_BITS) - 1))



#define GUCTL1              0xc11c      // Global User Control Register 1
#define USB31_IP_NAME       0xc120      // IP NAME REGISTER
#define GGPIO               0xc124      // Global General Purpose Input/Output Register
#define GUID                0xc128      // Global User ID Register
#define GUCTL               0xc12c      // Global User Control Register
#define GBUSERRADDR         0xc130      // Global Soc Bus Error Address Register
#define GBUSERRADDRLO       0xc130      // Global Soc Bus Error Address Register - Low
#define GBUSERRADDRHI       0xc134      // Global Soc Bus Error Address Register - High
#define GPRTBIMAP           0xc138      // Global SS Port to Bus Instance Mapping Register - Low
#define GPRTBIMAPHI         0xc13c      // Global SS Port to Bus Instance Mapping Register - High

#define GHWPARAMS0          0xc140      // Global Hardware Parameters Register 0
#define GHWPARAMS0_AWIDTH_START         24
#define GHWPARAMS0_AWIDTH_BITS          8
#define GHWPARAMS0_AWIDTH(p)            (((p) >> GHWPARAMS0_AWIDTH_START) & \
                                         ((1 << GHWPARAMS0_AWIDTH_BITS) - 1))
#define GHWPARAMS0_SDWIDTH_START        16
#define GHWPARAMS0_SDWIDTH_BITS         8
#define GHWPARAMS0_SDWIDTH(p)           (((p) >> GHWPARAMS0_SDWIDTH_START) & \
                                         ((1 << GHWPARAMS0_SDWIDTH_BITS) - 1))
#define GHWPARAMS0_MDWIDTH_START        8
#define GHWPARAMS0_MDWIDTH_BITS         8
#define GHWPARAMS0_MDWIDTH(p)           (((p) >> GHWPARAMS0_MDWIDTH_START) & \
                                         ((1 << GHWPARAMS0_MDWIDTH_BITS) - 1))
#define GHWPARAMS0_SBUS_TYPE_START      6
#define GHWPARAMS0_SBUS_TYPE_BITS       2
#define GHWPARAMS0_SBUS_TYPE(p)         (((p) >> GHWPARAMS0_SBUS_TYPE_START) & \
                                         ((1 << GHWPARAMS0_SBUS_TYPE_BITS) - 1))
#define GHWPARAMS0_MBUS_TYPE_START      3
#define GHWPARAMS0_MBUS_TYPE_BITS       3
#define GHWPARAMS0_MBUS_TYPE(p)         (((p) >> GHWPARAMS0_MBUS_TYPE_START) & \
                                         ((1 << GHWPARAMS0_MBUS_TYPE_BITS) - 1))
#define GHWPARAMS0_MODE_START           0
#define GHWPARAMS0_MODE_BITS            3
#define GHWPARAMS0_MODE(p)              (((p) >> GHWPARAMS0_MODE_START) & \
                                         ((1 << GHWPARAMS0_MODE_BITS) - 1))

#define GHWPARAMS1          0xc144              // Global Hardware Parameters Register 1
#define GHWPARAMS2          0xc148              // Global Hardware Parameters Register 2
#define GHWPARAMS3          0xc14c              // Global Hardware Parameters Register 3
#define GHWPARAMS4          0xc150              // Global Hardware Parameters Register 4
#define GHWPARAMS5          0xc154              // Global Hardware Parameters Register 5
#define GHWPARAMS6          0xc158              // Global Hardware Parameters Register 6
#define GHWPARAMS7          0xc15c              // Global Hardware Parameters Register 7
#define GDBGFIFOSPACE       0xc160              // Global Debug Queue/FIFO Space Available Register
#define GBMUCTL             0xc164              // Global BMU Control Register
#define GDBGBMU             0xc16c              // Global Debug BMU Register
#define GDBGLSPMUX_HST      0xc170              // Global Debug LSP MUX Register in host mode
#define GDBGLSPMUX_DEV      0xc170              // Global Debug LSP MUX Register
#define GDBGLSP             0xc174              // Global Debug LSP Register
#define GDBGEPINFO0         0xc178              // Global Debug Endpoint Information Register 0
#define GDBGEPINFO1         0xc17c              // Global Debug Endpoint Information Register 1
#define GPRTBIMAP_HS        0xc180              // Global High-Speed Port to Bus Instance Mapping Register
#define GPRTBIMAP_HSLO      0xc180              // Global High-Speed Port to Bus Instance Mapping Register - Low
#define GPRTBIMAP_HSHI      0xc184              // Global High-Speed Port to Bus Instance Mapping Register - High
#define GPRTBIMAP_FS        0xc188              // Global Full/Low-Speed Port to Bus Instance Mapping Register
#define GPRTBIMAP_FSLO      0xc188              // Global Full/Low-Speed Port to Bus Instance Mapping Register - Low
#define GPRTBIMAP_FSHI      0xc18c              // Global Full/Low-Speed Port to Bus Instance Mapping Register - High
#define GHMSOCBWOR          0xc190              // Global Host Mode SoC Bandwidth Override Register
#define GERRINJCTL_1        0xc194              // Global Error Injection 1 Control Register
#define GERRINJCTL_2        0xc194              // Global Error Injection 2 Control Register
#define USB31_VER_NUMBER    0xc1a0              // USB31 IP VERSION NUMBER
#define USB31_VER_TYPE      0xc1a4              // USB31 IP VERSION TYPE
#define GSYSBLKWINCTRL      0xc1b0              // System Bus Blocking Window Control
//#defineGUSB3RMMICTL(n)      varies
#define GUSB2PHYCFG(n)      (0xc200 + 4 * (n))  // Global USB2 PHY Configuration Register
#define GUSB2PHYCFG_PHYSOFTRST                  (1 << 31)
#define GUSB2PHYCFG_ULPI_LPM_WITH_OPMODE_CHK    (1 << 29)
#define GUSB2PHYCFG_HSIC_CON_WIDTH_ADJ(n)       (((n) & 0x3) << 27)
#define GUSB2PHYCFG_INV_SEL_HSIC                (1 << 26)
#define GUSB2PHYCFG_LSTRD(n)                    (((n) & 0x7) << 22)
#define GUSB2PHYCFG_LSIPD(n)                    (((n) & 0x7) << 19)
#define GUSB2PHYCFG_ULPIEXTVBUSINDICATOR        (1 << 18)
#define GUSB2PHYCFG_ULPIEXTVBUSDRV              (1 << 17)
#define GUSB2PHYCFG_ULPIAUTORES                 (1 << 15)
#define GUSB2PHYCFG_USBTRDTIM(n)                (((n) & 0xf) << 10)
#define GUSB2PHYCFG_USBTRDTIM_MASK              (0xf << 10)
#define GUSB2PHYCFG_XCVRDLY                     (1 << 9)
#define GUSB2PHYCFG_ENBLSLPM                    (1 << 8)
#define GUSB2PHYCFG_PHYSEL                      (1 << 7)
#define GUSB2PHYCFG_SUSPENDUSB20                (1 << 6)
#define GUSB2PHYCFG_FSINTF                      (1 << 5)
#define GUSB2PHYCFG_ULPI_UTMI_SEL               (1 << 4)
#define GUSB2PHYCFG_PHYIF                       (1 << 3)
#define GUSB2PHYCFG_TOUTCAL(n)                  (((n) & 0x7) << 0)

#define GUSB2I2CCTL(n)      (0xc240 + 4 * (n))  // Reserved Register
#define GUSB2PHYACC_UTMI(n) (0xc280 + 4 * (n))  // Global USB 2.0 UTMI PHY Vendor Control Register
#define GUSB2PHYACC_ULPI(n) (0xc280 + 4 * (n))  // Global USB 2.0 UTMI PHY Vendor Control Register

#define GUSB3PIPECTL(n)     (0xc2c0 + 4 * (n))      // Global USB 3.1 PIPE Control Register
#define GUSB3PIPECTL_PHY_SOFT_RST               (1 << 31)   // USB3 PHY Soft Reset
#define GUSB3PIPECTL_HST_PRT_CMPL               (1 << 30)
#define GUSB3PIPECTL_DIS_RX_DET_P3              (1 << 28)
#define GUSB3PIPECTL_UX_EXIT_IN_PX              (1 << 27)
#define GUSB3PIPECTL_PING_ENHANCE_EN            (1 << 26)
#define GUSB3PIPECTL_U1U2_EXIT_FAIL_TO_RECOV    (1 << 25)
#define GUSB3PIPECTL_REQUEST_P1P2P3             (1 << 24)
#define GUSB3PIPECTL_START_RX_DET_U3_RX_DET     (1 << 23)
#define GUSB3PIPECTL_DIS_RX_DET_U3_RX_DET       (1 << 22)
#define GUSB3PIPECTL_DELAY_P1P2P3(n)            (((n) & 0x7) << 19)
#define GUSB3PIPECTL_DELAYP1TRANS               (1 << 18)
#define GUSB3PIPECTL_SUSPENDENABLE              (1 << 17)
#define GUSB3PIPECTL_DATWIDTH(n)                (((n) & 0x3) << 15)
#define GUSB3PIPECTL_ABORT_RX_DET_IN_U2         (1 << 14)
#define GUSB3PIPECTL_SKIP_RX_DET                (1 << 13)
#define GUSB3PIPECTL_LFPS_P0_ALGN               (1 << 12)
#define GUSB3PIPECTL_P3P2_TRAN_OK               (1 << 11)
#define GUSB3PIPECTL_P3_EX_SIG_P3               (1 << 10)
#define GUSB3PIPECTL_LFPSFILTER                 (1 << 9)
#define GUSB3PIPECTL_RX_DETECT_TO_POLLING_LFPS_CONTROL (1 << 8)
#define GUSB3PIPECTL_SSIC_EN                    (1 << 7)
#define GUSB3PIPECTL_TX_SWING                   (1 << 6)
#define GUSB3PIPECTL_TX_MARGIN(n)               (((n) & 0x7) << 3)
#define GUSB3PIPECTL_SS_TX_DE_EMPHASIS(n)       (((n) & 0x3) << 1)
#define GUSB3PIPECTL_ELASTIC_BUFFER_MODE        (1 << 0)

#define GTXFIFOSIZ(n)       (0xc300 + 0x7c * (n))   // Global Transmit FIFO Size Register
#define GRXFIFOSIZ(n)       (0xc380 + 0x7c * (n))   // Global Receive FIFO Size Register
#define GEVNTADR(n)         (0xc400 + 0x10 * (n))   // Global Event Buffer Address Register
#define GEVNTADRLO(n)       (0xc400 + 0x10 * (n))   // Global Event Buffer Address Register - Low
#define GEVNTADRHI(n)       (0xc404 + 0x10 * (n))   // Global Event Buffer Address Register - High

#define GEVNTSIZ(n)         (0xc408 + 0x10 * (n))   // Global Event Buffer Size Register
#define GEVNTSIZ_EVNTINTRPTMASK         (1 << 31)   // Event Interrupt Mask

#define GEVNTCOUNT(n)       (0xc40c + 0x10 * (n))   // Global Event Buffer Size Register
#define GEVNTCOUNT_EVNT_HANDLER_BUSY    (1 << 31)   // Event Handler Busy
#define GEVNTCOUNT_EVNTCOUNT_MASK       0xffff      // Mask for Event Count

#define GHWPARAMS8          0xc600              // Global Hardware Parameters Register 8
#define GSMACCTL            0xc604              // Global SMAC CONTROL REGISTER
#define GUCTL2              0xc608              // Global User Control Register 2
#define GUCTL3              0xc60c              // Global User Control Register 3
#define GTXFIFOPRIDEV       0xc610              // Global Device TXFIFO DMA Priority Register
#define GTXFIFOPRIHST       0xc618              // Global Host TXFIFO DMA Priority Register
#define GRXFIFOPRIHST       0xc61c              // Global Host RXFIFO DMA Priority Register
#define GFIFOPRIDBC         0xc620              // Global Host Debug Capability DMA Priority Register
#define GDMAHLRATIO         0xc624              // Global Host FIFO DMA High-Low Priority Ratio Register
#define GOSTDDMA_ASYNC      0xc628              // Global Number of Async Outstanding DMA Register
#define GOSTDDMA_PRD        0xc62c              // Global Number of Periodic Outstanding DMA Register
#define GFLADJ              0xc630              // Global Frame Length Adjustment Register
#define GUSB2RHBCTL(n)      (0xc640 + 4 * (n))  // Global USB2 PHY Configuration Register

// Device mode register offsets
#define DCFG                            0xc700      // Device Configuration Register
#define DCFG_STOP_ON_DISCONNECT         (1 << 24)
#define DCFG_IGN_STRM_PP                (1 << 23)
#define DCFG_LPMCAP                     (1 << 22)
#define DCFG_NUMP_START                 17
#define DCFG_NUMP_BITS                  5
#define DCFG_INTRNUM_START              12
#define DCFG_INTRNUM_BITS               5
#define DCFG_DEVADDR_START              3
#define DCFG_DEVADDR_BITS               7
#define DCFG_DEVSPD_START               0
#define DCFG_DEVSPD_BITS                3
#define DCFG_DEVSPD_HIGH                0
#define DCFG_DEVSPD_FULL                1
#define DCFG_DEVSPD_LOW                 2
#define DCFG_DEVSPD_SUPER               4

#define DCTL                            0xc704      // Device Control Register
#define DCTL_RUN_STOP                   (1 << 31)
#define DCTL_CSFTRST                    (1 << 30)
#define DCFG_HIRDTHRES_START            24
#define DCFG_HIRDTHRES_BITS             5
#define DCFG_LPM_NYET_THRES_START       20
#define DCFG_LPM_NYET_THRES_BITS        4
#define DCTL_KEEP_CONNECT               (1 << 19)
#define DCTL_L1_HIBERNATION_EN          (1 << 18)
#define DCTL_CRS                        (1 << 17)
#define DCTL_CSS                        (1 << 16)
#define DCTL_INITU2ENA                  (1 << 12)
#define DCTL_ACCEPTU2ENA                (1 << 11)
#define DCTL_INITU1ENA                  (1 << 10)
#define DCTL_ACCEPTU1ENA                (1 << 9)
#define DCTL_ACCEPTU1ENA                (1 << 9)
#define DCFG_ULSTCHNGREQ_START          5
#define DCFG_ULSTCHNGREQ_BITS           4
#define DCFG_TSTCTL_START               1
#define DCFG_TSTCTL_BITS                4

#define DEVTEN                          0xc708      // Device Event Enable Register
#define DEVTEN_LDMEVTEN                 (1 << 15)
#define DEVTEN_L1WKUPEVTEN              (1 << 14)
#define DEVTEN_STOP_ON_DISCONNECT_EN    (1 << 13)
#define DEVTEN_VENDEVTSTRCVDEN          (1 << 12)
#define DEVTEN_ERRTICERREVTEN           (1 << 9)
#define DEVTEN_L1SUSPEN                 (1 << 8)
#define DEVTEN_SOFTEVTEN                (1 << 7)
#define DEVTEN_U3_L2_SUSP_EN            (1 << 6)
#define DEVTEN_HIBERNATION_REQ_EVT_EN   (1 << 5)
#define DEVTEN_WKUPEVTEN                (1 << 4)
#define DEVTEN_ULSTCNGEN                (1 << 3)
#define DEVTEN_CONNECTDONEEVTEN         (1 << 2)
#define DEVTEN_USBRSTEVTEN              (1 << 1)
#define DEVTEN_DISSCONNEVTEN            (1 << 0)

#define DSTS                            0xc70c      // Device Status Register
#define DSTS_DCNRD                      (1 << 29)
#define DSTS_SRE                        (1 << 28)
#define DSTS_RSS                        (1 << 25)
#define DSTS_SSS                        (1 << 24)
#define DSTS_COREIDLE                   (1 << 23)
#define DSTS_DEVCTRLHLT                 (1 << 22)
#define DSTS_USBLNKST_START             18
#define DSTS_USBLNKST_BITS              4
#define DSTS_USBLNKST(s)                (((s) >> DSTS_USBLNKST_START) & \
                                         ((1 << DSTS_USBLNKST_BITS) - 1))
#define DSTS_RXFIFOEMPTY                (1 << 17)
#define DSTS_SOFFN_START                3
#define DSTS_SOFFN_BITS                 14
#define DSTS_SOFFN(s)                   (((s) >> DSTS_SOFFN_START) & \
                                         ((1 << DSTS_SOFFN_BITS) - 1))
#define DSTS_CONNECTSPD_START           0
#define DSTS_CONNECTSPD_BITS            3
#define DSTS_CONNECTSPD(s)              (((s) >> DSTS_CONNECTSPD_START) & \
                                         ((1 << DSTS_CONNECTSPD_BITS) - 1))

// DSTS link state in SS node
#define DSTS_USBLNKST_U0                0x0
#define DSTS_USBLNKST_U1                0x1
#define DSTS_USBLNKST_U2                0x2
#define DSTS_USBLNKST_U3                0x3
#define DSTS_USBLNKST_ESS_DIS           0x4
#define DSTS_USBLNKST_RX_DET            0x5
#define DSTS_USBLNKST_ESS_INACT         0x6
#define DSTS_USBLNKST_POLL              0x7
#define DSTS_USBLNKST_RECOV             0x8
#define DSTS_USBLNKST_HRESET            0x9
#define DSTS_USBLNKST_CMPLY             0xa
#define DSTS_USBLNKST_LPBK              0xb
#define DSTS_USBLNKST_RESUME_RESET      0xf

// DSTS link state in HS/FS/LS node
#define DSTS_USBLNKST_ON                0x0
#define DSTS_USBLNKST_SLEEP             0x2
#define DSTS_USBLNKST_SUSPEND           0x3
#define DSTS_USBLNKST_DISCONNECTED      0x4
#define DSTS_USBLNKST_EARLY_SUSPEND     0x5
#define DSTS_USBLNKST_RESET             0xe
#define DSTS_USBLNKST_RESUME            0xf

// DSTS connection speed
#define DSTS_CONNECTSPD_HIGH            0
#define DSTS_CONNECTSPD_FULL            1
#define DSTS_CONNECTSPD_SUPER           4
#define DSTS_CONNECTSPD_ENHANCED_SUPER  5

#define DGCMDPAR                        0xc710      // Device Generic Command Parameter Register

#define DGCMD                           0xc714      // Device Generic Command Register
#define DGCMD_CMDSTATUS_START           12
#define DGCMD_CMDSTATUS_BITS            4
#define DGCMD_CMDACT                    (1 << 10)
#define DGCMD_CMDIOC                    (1 << 8)
#define DGCMD_CMDTYP_START              0
#define DGCMD_CMDTYP_BITS               8

#define DALEPENA            0xc720                  // Device Active USB Endpoint Enable Register
#define DLDMENA             0xc724                  // Device LDM Request Control Register

#define DEPCMDPAR2(n)       (0xc800 + 0x10 * (n))   // Endpoint-n Command Parameter 2 Register
#define DEPCMDPAR1(n)       (0xc804 + 0x10 * (n))   // Endpoint-n Command Parameter 1 Register
#define DEPCMDPAR0(n)       (0xc808 + 0x10 * (n))   // Endpoint-n Command Parameter 0 Register

#define DEPCMD(n)           (0xc80c + 0x10 * (n))   // Endpoint-n Command Parameter 0 Register
#define DEPCMD_COMMANDPARAM_START       16          // Command Parameters
#define DEPCMD_COMMANDPARAM_BITS        16
#define DEPCMD_CMDSTATUS_START          12          // Command Completion Status
#define DEPCMD_CMDSTATUS_BITS           4
#define DEPCMD_HIPRI_FORCERM            (1 << 11)   // HighPriority/ForceRM
#define DEPCMD_CMDACT                   (1 << 10)   // Command Active
#define DEPCMD_CMDIOC                   (1 << 8)    // Command Interrupt on Complete
#define DEPCMD_CMDTYP(n)                (((n) & 0xf) >> 0)

// Command Types for DEPCMD
#define DEPCFG                          1       // Set Endpoint Configuration
#define DEPXFERCFG                      2       // Set Endpoint Transfer Resource Configuration
#define DEPGETSTATE                     3       // Get EndpointState 
#define DEPSSTALL                       4       // Set Stall
#define DEPCSTALL                       5       // Clear Stall
#define DEPSTRTXFER                     6       // Start Transfer
#define DEPUPDXFER                      7       // Update Transfer
#define DEPENDXFER                      8       // End Transfer
#define DEPSTARTCFG                     9       // Start New Configuration

#define DEPCMD_RESOURCE_INDEX(n)        (((n) & 0x7f) << 16)

// DEPCFG Params 0
#define DEPCFG_ACTION_INITIALIZE        (0 << 30)
#define DEPCFG_ACTION_RESTORE           (1 << 30)
#define DEPCFG_ACTION_MODIFY            (2 << 30)
#define DEPCFG_BURST_SIZE(n)            ((((n) - 1) & 0xf) << 22)
#define DEPCFG_FIFO_NUM(n)              (((n) & 0x1f) << 17)
#define DEPCFG_INTERNAL_RETRY           (1 << 15)
#define DEPCFG_MAX_PACKET_SIZE(n)       (((n) & 0x7ff) << 3)
#define DEPCFG_EP_TYPE(n)               (((n) & 0x3) << 1)

// DEPCFG Params 1
#define DEPCFG_FIFO_BASED               (1 << 31)
#define DEPCFG_EP_NUMBER(n)             (((n) & 0x1f) << 25)
#define DEPCFG_STREAM_CAPABLE           (1 << 24)
#define DEPCFG_INTERVAL(n)              (((n) & 0xff) << 16)
#define DEPCFG_EBC                      (1 << 15)   // External Buffer Control
#define DEPCFG_EBC_NO_WRITE_BACK        (1 << 14)   // Don't write back HWO bit to the TRB descriptor
#define DEPCFG_STREAM_EVT_EN            (1 << 13)
#define DEPCFG_XFER_NOT_READY_EN        (1 << 10)
#define DEPCFG_XFER_IN_PROGRESS_EN      (1 << 9)
#define DEPCFG_XFER_COMPLETE_EN         (1 << 8)
#define DEPCFG_INTR_NUM(n)              (((n) & 0x1f) << 0)

// DEPXFERCFG Params 0
#define DEPXFERCFG_NUM_XFER_RES(n)      (((n) & 0xff) << 0)

#define DEV_IMOD(n)                     (0xca00 + 4 * (n))  // Device Interrupt Moderation Register

// OTG and Battery Charger register offsets
#define OCFG                        0xcc00          // OTG Configuration Register
#define OCFG_DISPRTPWRCUTOFF        (1 << 5)
#define OCFG_OTGHIBDISMASK          (1 << 4)
#define OCFG_OTGSFTRSTMSK           (1 << 3)
#define OCFG_HNPCAP                 (1 << 1)
#define OCFG_SRPCAP                 (1 << 0)

#define OCTL                        0xcc04          // OTG Control Register
#define OCTL_OTG3_GOERR             (1 << 7)
#define OCTL_PERIMODE               (1 << 6)
#define OCTL_PRTPWRCTL              (1 << 5)
#define OCTL_HNPREQ                 (1 << 4)
#define OCTL_SESREQ                 (1 << 3)
#define OCTL_TERMSELDLPULSE         (1 << 2)
#define OCTL_DEVSETHNPEN            (1 << 1)
#define OCTL_HSTSETHNPEN            (1 << 0)

#define OEVT                        0xcc08          // OTG Event Register
#define OEVT_DEVICEMOD              (1 << 31)
#define OEVT_OTGXHCIRUNSTPSETEVNT   (1 << 27)
#define OEVT_OTGDEVRUNSTPSETEVNT    (1 << 26)
#define OEVT_OTGHIBENTRYEVNT        (1 << 25)
#define OEVT_OTGCONIDSTSCHNGEVNT    (1 << 24)
#define OEVT_HRRCONFNOTIFEVNT       (1 << 23)
#define OEVT_HRRINITNOTIFEVNT       (1 << 22)
#define OEVT_OTGADEVIDLEEVNT        (1 << 21)
#define OEVT_OTGADEVBHOSTENDEVNT    (1 << 20)
#define OEVT_OTGADEVHOSTEVNT        (1 << 19)
#define OEVT_OTGADEVHNPCHNGEVNT     (1 << 18)
#define OEVT_OTGADEVSRPDETEVNT      (1 << 17)
#define OEVT_OTGADEVSESSENDDETEVNT  (1 << 16)
#define OEVT_OTGBDEVBHOSTENDEVNT    (1 << 11)
#define OEVT_OTGBDEVHNPCHNGEVNT     (1 << 10)
#define OEVT_OTGBDEVSESSVLDDETEVNT  (1 << 9)
#define OEVT_OTGBDEVVBUSCHNGEVNT    (1 << 8)

#define OEVTEN                      0xcc0c          // OTG Event Enable Register

#define OSTS                        0xcc10          // OTG Status Register
#define OSTS_OTGSTATE_START         8
#define OSTS_OTGSTATE_BITS          4
#define OSTS_PERIPHERALSTATE       (1 << 4)
#define OSTS_XHCIPRTPOWER          (1 << 3)
#define OSTS_BSESVLD               (1 << 2)
#define OSTS_ASESVLD               (1 << 1)
#define OSTS_CONIDSTS              (1 << 0)

#define BCFG                        0xcc30          // BC Configuration Register
#define BCFG_IDDIG_SEL              (1 << 1)        // IDDIG Select
#define BCFG_CHIRP_EN               (1 << 0)        // Chirp Enable

#define BCEVT                       0xcc38          // BC Event Register
#define BCEVT_MV_CHNG_EVNT          (1 << 24)       // Multi-Valued Input Changed Event
#define BCEVT_MULT_VAL_ID_BC(e)     ((e) & 0x1f)    // Multi-Valued ID Pin

#define BCEVTEN                     0xcc3c          // BC Event Enable Register
#define BCEVTEN_CHNG_EVNT_ENA       (1 << 24)       // Multi-Valued Input Changed Event Enable

// Link register offsets
#define LU1LFPSRXTIM(n)     (0xd000 + 0x80 * (n))   // U1_LFPS_RX_TIMER_REG
#define LU1LFPSTXTIM(n)     (0xd004 + 0x80 * (n))   // U1 LFPS TX TIMER REGISTER
#define LU2LFPSRXTIM(n)     (0xd008 + 0x80 * (n))   // U1 LFPS RX TIMER REGISTER
#define LU2LFPSTXTIM(n)     (0xd00c + 0x80 * (n))   // U2 LFPS TX TIMER REG REGISTER
#define LU3LFPSRXTIM(n)     (0xd010 + 0x80 * (n))   // U3 LFPS RX TIMER REGS REGISTER
#define LU3LFPSTXTIM(n)     (0xd014 + 0x80 * (n))   // U3 LFPS TX TIMER REGS REGISTER
#define LPINGLFPSTIM(n)     (0xd018 + 0x80 * (n))   // PING LFPS TIMER REGISTER
#define LPOLLLFPSTXTIM(n)   (0xd01c + 0x80 * (n))   // POLL LFPS TX TIMER REGISTER
#define LSKIPFREQ(n)        (0xd020 + 0x80 * (n))   // SKIP FREQUENCY REGISTER
#define LLUCTL(n)           (0xd024 + 0x80 * (n))   // TX TS1 COUNT REGISTER
#define LPTMDPDELAY(n)      (0xd028 + 0x80 * (n))   // PTM DATAPATH DELAY REGISTER
#define LSCDTIM1(n)         (0xd02c + 0x80 * (n))   // SCD TIMER 1 REGISTER
#define LSCDTIM2(n)         (0xd030 + 0x80 * (n))   // SCD TIMER 2 REGISTER
#define LSCDTIM3(n)         (0xd034 + 0x80 * (n))   // SCD TIMER 3 REGISTER
#define LSCDTIM4(n)         (0xd038 + 0x80 * (n))   // SCD TIMER 4 REGISTER
#define LLPBMTIM1(n)        (0xd03c + 0x80 * (n))   // LPBM TIMER 1 REGISTER
#define LLPBMTIM2(n)        (0xd040 + 0x80 * (n))   // LPBM TIMER 2 REGISTER
#define LLPBMTXTIM(n)       (0xd044 + 0x80 * (n))   // LPBM TX TIMER REGISTER
#define LLINKERRINJ(n)      (0xd048 + 0x80 * (n))   // LINK ERROR TYPE INJECT REGISTER
#define LLINKERRINJEN(n)    (0xd04c + 0x80 * (n))   // LINK ERROR INJECT ENABLE REGISTER
#define GDBGLTSSM(n)        (0xd050 + 0x80 * (n))   // Global Debug LTSSM Register
#define GDBGLNMCC(n)        (0xd054 + 0x80 * (n))   // Global Debug LNMCC Register
#define LLINKDBGCTRL(n)     (0xd058 + 0x80 * (n))   // LINK DEBUG CONTROL REGISTER
#define LLINKDBGCNTTRIG(n)  (0xd05c + 0x80 * (n))   // LINK DEBUG COUNT TRIGGER REGISTER
#define LCSR_TX_DEEMPH(n)   (0xd060 + 0x80 * (n))   // LCSR_TX_DEEMPH REGISTER
#define LCSR_TX_DEEMPH_1(n) (0xd064 + 0x80 * (n))   // LCSR_TX_DEEMPH_1 REGISTER
#define LCSR_TX_DEEMPH_2(n) (0xd068 + 0x80 * (n))   // LCSR_TX_DEEMPH_2 REGISTER
#define LCSR_TX_DEEMPH_3(n) (0xd06c + 0x80 * (n))   // LCSR_TX_DEEMPH_3 REGISTER
#define LCSRPTMDEBUG1(n)    (0xd070 + 0x80 * (n))   // LCSRPTMDEBUG1 REGISTER
#define LCSRPTMDEBUG2(n)    (0xd074 + 0x80 * (n))   // LCSRPTMDELAY2 REGISTER
