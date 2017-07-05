// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

typedef struct sdhci_regs {
    uint32_t arg2;          // 00h
    uint32_t blkcntsiz;     // 04h
    uint32_t arg1;          // 08h
    uint32_t cmd;           // 0Ch
#define SDHCI_XFERMODE_DMA_ENABLE  (1 << 0)

    uint32_t resp0;         // 10h
    uint32_t resp1;         // 14h
    uint32_t resp2;         // 18h
    uint32_t resp3;         // 1Ch
    uint32_t data;          // 20h
    uint32_t state;         // 24h
#define SDHCI_STATE_CMD_INHIBIT           (1 << 0)
#define SDHCI_STATE_DAT_INHIBIT           (1 << 1)
#define SDHCI_STATE_DAT_LINE_ACTIVE       (1 << 2)
#define SDHCI_STATE_RETUNING_REQUEST      (1 << 3)
#define SDHCI_STATE_WRITE_TRANSFER_ACTIVE (1 << 8)
#define SDHCI_STATE_READ_TRANSFER_ACTIVE  (1 << 9)
#define SDHCI_STATE_BUFFER_WRITE_ENABLE   (1 << 10)
#define SDHCI_STATE_BUFFER_READ_ENABLE    (1 << 11)
#define SDHCI_STATE_CARD_INSERTED         (1 << 16)
#define SDHCI_STATE_CARD_STATE_STABLE     (1 << 17)
#define SDHCI_STATE_CARD_DETECT_PIN_LEVEL (1 << 18)
#define SDHCI_STATE_WRITE_PROTECT         (1 << 19)
#define SDHCI_STATE_CMD_LINE_SIGNAL_LVL   (1 << 24)

    uint32_t ctrl0;         // 28h
#define SDHCI_HOSTCTRL_LED_ON              (1 << 0)
#define SDHCI_HOSTCTRL_FOUR_BIT_BUS_WIDTH  (1 << 1)
#define SDHCI_HOSTCTRL_HIGHSPEED_ENABLE    (1 << 2)
#define SDHCI_HOSTCTRL_DMA_SELECT_ADMA2    (3 << 3)
#define SDHCI_HOSTCTRL_EXT_DATA_WIDTH      (1 << 5)
#define SDHCI_PWRCTRL_SD_BUS_POWER         (1 << 8)
#define SDHCI_PWRCTRL_SD_BUS_VOLTAGE_MASK  (7 << 9)
#define SDHCI_PWRCTRL_SD_BUS_VOLTAGE_3P3V  (7 << 9)
#define SDHCI_PWRCTRL_SD_BUS_VOLTAGE_3P0V  (6 << 9)
#define SDHCI_PWRCTRL_SD_BUS_VOLTAGE_1P8V  (5 << 9)
#define SDHCI_EMMC_HW_RESET                (1 << 12)

    uint32_t ctrl1;         // 2Ch
#define SDHCI_INTERNAL_CLOCK_ENABLE        (1 << 0)
#define SDHCI_INTERNAL_CLOCK_STABLE        (1 << 1)
#define SDHCI_SD_CLOCK_ENABLE              (1 << 2)
#define SDHCI_PROGRAMMABLE_CLOCK_GENERATOR (1 << 5)
#define SDHCI_SOFTWARE_RESET_ALL           (1 << 24)
#define SDHCI_SOFTWARE_RESET_CMD           (1 << 25)
#define SDHCI_SOFTWARE_RESET_DAT           (1 << 26)

    uint32_t irq;           // 30h
    uint32_t irqmsk;        // 34h
    uint32_t irqen;         // 38h
#define SDHCI_IRQ_CMD_CPLT         (1 << 0)
#define SDHCI_IRQ_XFER_CPLT        (1 << 1)
#define SDHCI_IRQ_BLK_GAP_EVT      (1 << 2)
#define SDHCI_IRQ_DMA              (1 << 3)
#define SDHCI_IRQ_BUFF_WRITE_READY (1 << 4)
#define SDHCI_IRQ_BUFF_READ_READY  (1 << 5)
#define SDHCI_IRQ_CARD_INSERTION   (1 << 6)
#define SDHCI_IRQ_CARD_REMOVAL     (1 << 7)
#define SDHCI_IRQ_CARD_INTERRUPT   (1 << 8)
#define SDHCI_IRQ_A                (1 << 9)
#define SDHCI_IRQ_B                (1 << 10)
#define SDHCI_IRQ_C                (1 << 11)
#define SDHCI_IRQ_RETUNING         (1 << 12)
#define SDHCI_IRQ_ERR              (1 << 15)

#define SDHCI_IRQ_ERR_CMD_TIMEOUT   (1 << 16)
#define SDHCI_IRQ_ERR_CMD_CRC       (1 << 17)
#define SDHCI_IRQ_ERR_CMD_END_BIT   (1 << 18)
#define SDHCI_IRQ_ERR_CMD_INDEX     (1 << 19)
#define SDHCI_IRQ_ERR_CMD           (SDHCI_IRQ_ERR_CMD_TIMEOUT | \
                                     SDHCI_IRQ_ERR_CMD_CRC | \
                                     SDHCI_IRQ_ERR_CMD_END_BIT | \
                                     SDHCI_IRQ_ERR_CMD_INDEX)
#define SDHCI_IRQ_ERR_DAT_TIMEOUT   (1 << 20)
#define SDHCI_IRQ_ERR_DAT_CRC       (1 << 21)
#define SDHCI_IRQ_ERR_DAT_ENDBIT    (1 << 22)
#define SDHCI_IRQ_ERR_DAT           (SDHCI_IRQ_ERR_DAT_TIMEOUT | \
                                     SDHCI_IRQ_ERR_DAT_CRC | \
                                     SDHCI_IRQ_ERR_DAT_ENDBIT)
#define SDHCI_IRQ_ERR_CURRENT_LIMIT (1 << 23)
#define SDHCI_IRQ_ERR_AUTO_CMD      (1 << 24)
#define SDHCI_IRQ_ERR_ADMA          (1 << 25)
#define SDHCI_IRQ_ERR_TUNING        (1 << 26)
#define SDHCI_IRQ_ERR_VS_1          (1 << 28)
#define SDHCI_IRQ_ERR_VS_2          (1 << 29)
#define SDHCI_IRQ_ERR_VS_3          (1 << 30)
#define SDHCI_IRQ_ERR_VS_4          (1 << 31)
    uint32_t ctrl2;         // 3Ch
#define SDHCI_HOSTCTRL2_UHS_MODE_SELECT_MASK    (7 << 16)
#define SDHCI_HOSTCTRL2_UHS_MODE_SELECT_SDR12   (0 << 16)
#define SDHCI_HOSTCTRL2_UHS_MODE_SELECT_SDR25   (1 << 16)
#define SDHCI_HOSTCTRL2_UHS_MODE_SELECT_SDR50   (2 << 16)
#define SDHCI_HOSTCTRL2_UHS_MODE_SELECT_SDR104  (3 << 16)
#define SDHCI_HOSTCTRL2_UHS_MODE_SELECT_DDR50   (4 << 16)
#define SDHCI_HOSTCTRL2_UHS_MODE_SELECT_HS400   (5 << 16)
#define SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA     (1 << 19)

    uint32_t caps0;         // 40h
#define SDHCI_CORECFG_8_BIT_SUPPORT    (1 << 18)
#define SDHCI_CORECFG_ADMA2_SUPPORT    (1 << 19)
#define SDHCI_CORECFG_3P3_VOLT_SUPPORT (1 << 24)
#define SDHCI_CORECFG_3P0_VOLT_SUPPORT (1 << 25)
#define SDHCI_CORECFG_1P8_VOLT_SUPPORT (1 << 26)
#define SDHCI_CORECFG_64BIT_SUPPORT    (1 << 28)

    uint32_t caps1;         // 44h
    uint32_t maxcaps0;      // 48h
    uint32_t maxcaps1;      // 4Ch
    uint32_t forceirq;      // 50h
    uint32_t admaerr;       // 54h
    uint32_t admaaddr0;     // 58h
    uint32_t admaaddr1;     // 5Ch
    uint32_t preset[4];     // 60h
    uint8_t  resvd[112];
    uint32_t busctl;

    uint8_t _reserved_4[24];

    uint32_t slotirqversion;
#define SDHCI_VERSION_1 0x00
#define SDHCI_VERSION_2 0x01
#define SDHCI_VERSION_3 0x02
} __PACKED sdhci_regs_t;
