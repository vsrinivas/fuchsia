// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

typedef struct imx_sdhci_regs {
    uint32_t ds_addr;                       // 0x00
    uint32_t blk_att;                       // 0x04
    uint32_t cmd_arg;                       // 0x08
    uint32_t cmd_xfr_typ;                   // 0x0C
    uint32_t cmd_rsp0;                      // 0x10
    uint32_t cmd_rsp1;                      // 0x14
    uint32_t cmd_rsp2;                      // 0x18
    uint32_t cmd_rsp3;                      // 0x1C
    uint32_t data_buff_acc_port;            // 0x20
    uint32_t pres_state;                    // 0x24
    uint32_t prot_ctrl;                     // 0x28
    uint32_t sys_ctrl;                      // 0x2C
    uint32_t int_status;                    // 0x30
    uint32_t int_status_en;                 // 0x34
    uint32_t int_signal_en;                 // 0x38
    uint32_t autocmd12_err_status;          // 0x3C
    uint32_t host_ctrl_cap;                 // 0x40
    uint32_t wtmk_lvl;                      // 0x44
    uint32_t mix_ctrl;                      // 0x48
    uint8_t rsv1[4];                        // xxxxxxxx
    uint32_t force_event;                   // 0x50
    uint32_t adma_err_status;               // 0x54
    uint32_t adma_sys_addr;                 // 0x58
    uint8_t rsv2[4];                        // xxxxxxxx
    uint32_t dll_ctrl;                      // 0x60
    uint32_t dll_status;                    // 0x64
    uint32_t clk_tune_ctrl_status;          // 0x68
    uint8_t rsv3[4];                        // xxxxxxxx
    uint32_t strobe_dll_ctrl;               // 0x70
    uint32_t strobe_dll_status;             // 0x74
    uint8_t rsv4[72];                       // xxxxxxxx
    uint32_t vend_spec;                     // 0xC0
    uint32_t mmc_boot;                      // 0xC4
    uint32_t vend_spec2;                    // 0xC8
    uint32_t tuning_ctrl;                   // 0xCC
} __PACKED imx_sdhci_regs_t;


#define IMX_SDHC_DS_ADDR(x)                         (x << 2)

#define IMX_SDHC_BLK_ATT_BLKSIZE(x)                 ( x & 0x1FFF)
#define IMX_SDHC_BLK_ATT_BLKCNT(x)                  ((x & 0xFFFF) << 16)


#define IMX_SDHC_CMD_XFER_TYPE_CMDINX(x)            ((x & 0x3f) << 24)
#define IMX_SDHC_CMD_XFER_TYPE_CMDTYP_ABORT         (3 << 22)
#define IMX_SDHC_CMD_XFER_TYPE_CMDTYP_RESUME        (2 << 22)
#define IMX_SDHC_CMD_XFER_TYPE_CMDTYP_SUSPEND       (1 << 22)
#define IMX_SDHC_CMD_XFER_TYPE_CMDTYP_NORM          (0 << 22)
#define IMX_SDHC_CMD_XFER_TYPE_DPSEL                (1 << 21)
#define IMX_SDHC_CMD_XFER_TYPE_CICEN                (1 << 20)
#define IMX_SDHC_CMD_XFER_TYPE_CCCEN                (1 << 19)
#define IMX_SDHC_CMD_XFER_TYPE_RSP_TYPE_48_BSY      (3 << 16)
#define IMX_SDHC_CMD_XFER_TYPE_RSP_TYPE_48          (2 << 16)
#define IMX_SDHC_CMD_XFER_TYPE_RSP_TYPE_136         (1 << 16)
#define IMX_SDHC_CMD_XFER_TYPE_RSP_TYPE_NO_RESP     (0 << 16)
#define IMX_SDHC_CMD_XFER_TYPE_CMD_MASK             (0xFFFF << 16)

#define IMX_SDHC_PRES_STATE_DLSL(x)                 (x << 24)
#define IMX_SDHC_PRES_STATE_CLSL                    (1 << 23)
#define IMX_SDHC_PRES_STATE_WPSPL                   (1 << 19)
#define IMX_SDHC_PRES_STATE_CDPL                    (1 << 18)
#define IMX_SDHC_PRES_STATE_CINST                   (1 << 16)
#define IMX_SDHC_PRES_STATE_TSCD                    (1 << 15)
#define IMX_SDHC_PRES_STATE_RTR                     (1 << 12)
#define IMX_SDHC_PRES_STATE_BREN                    (1 << 11)
#define IMX_SDHC_PRES_STATE_BWEN                    (1 << 10)
#define IMX_SDHC_PRES_STATE_RTA                     (1 << 9)
#define IMX_SDHC_PRES_STATE_WTA                     (1 << 8)
#define IMX_SDHC_PRES_STATE_SDOFF                   (1 << 7)
#define IMX_SDHC_PRES_STATE_PEROFF                  (1 << 6)
#define IMX_SDHC_PRES_STATE_HCKOFF                  (1 << 5)
#define IMX_SDHC_PRES_STATE_IPGOFF                  (1 << 4)
#define IMX_SDHC_PRES_STATE_SDSTB                   (1 << 3)
#define IMX_SDHC_PRES_STATE_DLA                     (1 << 2)
#define IMX_SDHC_PRES_STATE_CDIHB                   (1 << 1)
#define IMX_SDHC_PRES_STATE_CIHB                    (1 << 0)


#define IMX_SDHC_PROT_CTRL_DMASEL_MASK              (3 << 8)
#define IMX_SDHC_PROT_CTRL_DMASEL_ADMA2             (2 << 8)
#define IMX_SDHC_PROT_CTRL_DMASEL_ADMA1             (1 << 8)
#define IMX_SDHC_PROT_CTRL_DMASEL_NODMA             (0 << 8)
#define IMX_SDHC_PROT_CTRL_CDSS                     (1 << 7)
#define IMX_SDHC_PROT_CTRL_CDTL                     (1 << 6)
#define IMX_SDHC_PROT_CTRL_INIT                     (1 << 5)
#define IMX_SDHC_PROT_CTRL_DTW_MASK                 (3 << 1)
#define IMX_SDHC_PROT_CTRL_DTW_8                    (2 << 1)
#define IMX_SDHC_PROT_CTRL_DTW_4                    (1 << 1)
#define IMX_SDHC_PROT_CTRL_DTW_1                    (0 << 1)

#define IMX_SDHC_SYS_CTRL_RSTT                      (1 << 28)
#define IMX_SDHC_SYS_CTRL_INTA                      (1 << 27)
#define IMX_SDHC_SYS_CTRL_RSTD                      (1 << 26)
#define IMX_SDHC_SYS_CTRL_RSTC                      (1 << 25)
#define IMX_SDHC_SYS_CTRL_RSTA                      (1 << 24)
#define IMX_SDHC_SYS_CTRL_DTOCV_MASK                (0xf << 16)
#define IMX_SDHC_SYS_CTRL_DTOCV(x)                  (x << 16)
#define IMX_SDHC_SYS_CTRL_SDCLKFS(x)                ((x & 0xFF) << 8)
#define IMX_SDHC_SYS_CTRL_DVS(x)                    ((x & 0xF) << 4)

// Undocumented sysctl bits (clock related)
#define IMX_SDHC_SYS_CTRL_CLOCK_MASK                (0x0000fff0)
#define IMX_SDHC_SYS_CTRL_PREDIV_SHIFT              (8)
#define IMX_SDHC_SYS_CTRL_DIVIDER_SHIFT             (4)
#define IMX_SDHC_SYS_CTRL_CLOCK_PEREN               (1 << 2)
#define IMX_SDHC_SYS_CTRL_CLOCK_HCKEN               (1 << 1)
#define IMX_SDHC_SYS_CTRL_CLOCK_IPGEN               (1 << 0)

#define IMX_SDHC_INT_STAT_DMAE                      (1 << 28)
#define IMX_SDHC_INT_STAT_TNE                       (1 << 26)
#define IMX_SDHC_INT_STAT_AC12E                     (1 << 24)
#define IMX_SDHC_INT_STAT_DEBE                      (1 << 22)
#define IMX_SDHC_INT_STAT_DCE                       (1 << 21)
#define IMX_SDHC_INT_STAT_DTOE                      (1 << 20)
#define IMX_SDHC_INT_STAT_CIE                       (1 << 19)
#define IMX_SDHC_INT_STAT_CEBE                      (1 << 18)
#define IMX_SDHC_INT_STAT_CCE                       (1 << 17)
#define IMX_SDHC_INT_STAT_CTOE                      (1 << 16)
#define IMX_SDHC_INT_STAT_TP                        (1 << 14)
#define IMX_SDHC_INT_STAT_RTE                       (1 << 12)
#define IMX_SDHC_INT_STAT_CINT                      (1 << 8)
#define IMX_SDHC_INT_STAT_CRM                       (1 << 7)
#define IMX_SDHC_INT_STAT_CINS                      (1 << 6)
#define IMX_SDHC_INT_STAT_BRR                       (1 << 5)
#define IMX_SDHC_INT_STAT_BWR                       (1 << 4)
#define IMX_SDHC_INT_STAT_DINT                      (1 << 3)
#define IMX_SDHC_INT_STAT_BGE                       (1 << 2)
#define IMX_SDHC_INT_STAT_TC                        (1 << 1)
#define IMX_SDHC_INT_STAT_CC                        (1 << 0)

#define IMX_SDHC_INT_EN_DMAEN                       (1 << 28)
#define IMX_SDHC_INT_EN_TNE                         (1 << 26)
#define IMX_SDHC_INT_EN_AC12E                       (1 << 24)
#define IMX_SDHC_INT_EN_DEBE                        (1 << 22)
#define IMX_SDHC_INT_EN_DCE                         (1 << 21)
#define IMX_SDHC_INT_EN_DTOE                        (1 << 20)
#define IMX_SDHC_INT_EN_CIE                         (1 << 19)
#define IMX_SDHC_INT_EN_CEBE                        (1 << 18)
#define IMX_SDHC_INT_EN_CCE                         (1 << 17)
#define IMX_SDHC_INT_EN_CTOE                        (1 << 16)
#define IMX_SDHC_INT_EN_TP                          (1 << 14)
#define IMX_SDHC_INT_EN_RTE                         (1 << 12)
#define IMX_SDHC_INT_EN_CINT                        (1 << 8)
#define IMX_SDHC_INT_EN_CRM                         (1 << 7)
#define IMX_SDHC_INT_EN_CINS                        (1 << 6)
#define IMX_SDHC_INT_EN_BRR                         (1 << 5)
#define IMX_SDHC_INT_EN_BWR                         (1 << 4)
#define IMX_SDHC_INT_EN_DINT                        (1 << 3)
#define IMX_SDHC_INT_EN_BGE                         (1 << 2)
#define IMX_SDHC_INT_EN_TC                          (1 << 1)
#define IMX_SDHC_INT_EN_CC                          (1 << 0)

#define IMX_SDHC_INT_SIG_DMAEN                     (1 << 28)
#define IMX_SDHC_INT_SIG_TNE                       (1 << 26)
#define IMX_SDHC_INT_SIG_AC12E                     (1 << 24)
#define IMX_SDHC_INT_SIG_DEBE                      (1 << 22)
#define IMX_SDHC_INT_SIG_DCE                       (1 << 21)
#define IMX_SDHC_INT_SIG_DTOE                      (1 << 20)
#define IMX_SDHC_INT_SIG_CIE                       (1 << 19)
#define IMX_SDHC_INT_SIG_CEBE                      (1 << 18)
#define IMX_SDHC_INT_SIG_CCE                       (1 << 17)
#define IMX_SDHC_INT_SIG_CTOE                      (1 << 16)
#define IMX_SDHC_INT_SIG_TP                        (1 << 14)
#define IMX_SDHC_INT_SIG_RTE                       (1 << 12)
#define IMX_SDHC_INT_SIG_CINT                      (1 << 8)
#define IMX_SDHC_INT_SIG_CRM                       (1 << 7)
#define IMX_SDHC_INT_SIG_CINS                      (1 << 6)
#define IMX_SDHC_INT_SIG_BRR                       (1 << 5)
#define IMX_SDHC_INT_SIG_BWR                       (1 << 4)
#define IMX_SDHC_INT_SIG_DINT                      (1 << 3)
#define IMX_SDHC_INT_SIG_BGE                       (1 << 2)
#define IMX_SDHC_INT_SIG_TC                        (1 << 1)
#define IMX_SDHC_INT_SIG_CC                        (1 << 0)

#define IMX_SDHC_HOST_CTRL_CAP_VS18                 (1 << 26)
#define IMX_SDHC_HOST_CTRL_CAP_VS30                 (1 << 25)
#define IMX_SDHC_HOST_CTRL_CAP_VS33                 (1 << 24)
#define IMX_SDHC_HOST_CTRL_CAP_SRS                  (1 << 23)
#define IMX_SDHC_HOST_CTRL_CAP_DMAS                 (1 << 22)
#define IMX_SDHC_HOST_CTRL_CAP_HSS                  (1 << 21)
#define IMX_SDHC_HOST_CTRL_CAP_ADMAS                (1 << 20)

#define IMX_SDHC_MIX_CTRL_HS400                     (1 << 26)
#define IMX_SDHC_MIX_CTRL_FBCLK_SEL                 (1 << 25)
#define IMX_SDHC_MIX_CTRL_AUTO_TUNE                 (1 << 24)
#define IMX_SDHC_MIX_CTRL_SMP_CLK_SEL               (1 << 23)
#define IMX_SDHC_MIX_CTRL_EXE_TUNE                  (1 << 22)
#define IMX_SDHC_MIX_CTRL_AC23EN                    (1 << 7)
#define IMX_SDHC_MIX_CTRL_NIBBLE_POS                (1 << 6)
#define IMX_SDHC_MIX_CTRL_MSBSEL                    (1 << 5)
#define IMX_SDHC_MIX_CTRL_DTDSEL                    (1 << 4)
#define IMX_SDHC_MIX_CTRL_DDR_EN                    (1 << 3)
#define IMX_SDHC_MIX_CTRL_AC12EN                    (1 << 2)
#define IMX_SDHC_MIX_CTRL_BCEN                      (1 << 1)
#define IMX_SDHC_MIX_CTRL_DMAEN                     (1 << 0)
#define IMX_SDHC_MIX_CTRL_CMD_MASK                  (0xb6 << 0)

#define IMX_SDHC_VEND_SPEC_CARD_CLK_SOFT_EN         (1 << 14)
#define IMX_SDHC_VEND_SPEC_IPG_PERCLK_SOFT_EN       (1 << 13)
#define IMX_SDHC_VEND_SPEC_HCLK_SOFT_EN             (1 << 12)
#define IMX_SDHC_VEND_SPEC_IPG_CLK_SOFT_EN          (1 << 11)
#define IMX_SDHC_VEND_SPEC_FRC_SDCLK_ON             (1 << 8)
#define IMX_SDHC_VEND_SPEC_INIT                     (0x20007809)

#define IMX_SDHC_AUTOCMD12_ERRSTS_SMP_CLK_SEL       (1 << 23)
#define IMX_SDHC_AUTOCMD12_ERRSTS_EXE_TUNING        (1 << 22)

#define IMX_SDHC_DLLCTRL_SLV_DLY_TARGET             (0x5 << 3)
#define IMX_SDHC_DLLCTRL_RESET                      (1 << 1)
#define IMX_SDHC_DLLCTRL_ENABLE                     (1 << 0)

#define IMX_SDHC_DLLSTS_REF_LOCK                    (1 << 1)
#define IMX_SDHC_DLLSTS_SLV_LOCK                    (1 << 0)

#define IMX_SDHC_TUNING_CTRL_START_TAP_MASK         (0xFF << 0)
#define IMX_SDHC_TUNING_CTRL_START_TAP(x)           (x << 0)
#define IMX_SDHC_TUNING_CTRL_STEP_MASK              (0x7 << 16)
#define IMX_SDHC_TUNING_CTRL_STEP(x)                (x << 16)
#define IMX_SDHC_TUNING_CTRL_STD_TUN_EN             (1 << 24)