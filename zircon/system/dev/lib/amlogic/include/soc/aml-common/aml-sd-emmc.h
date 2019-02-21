// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/io-buffer.h>
#include <string.h>

#pragma once

//From EMMC Design documentation provided by AMLOGIC
#define AML_SD_EMMC_IRQ_ALL_CLEAR            0x3fff
#define AML_SD_EMMC_CTS_OSCIN_CLK_FREQ       24000000   //24MHz
#define AML_SD_EMMC_CTS_OSCIN_CLK_SRC        0
#define AML_SD_EMMC_FCLK_DIV2_FREQ           1000000000 //1GHz
#define AML_SD_EMMC_FCLK_DIV2_SRC            1
//~Min freq attainable with DIV2 Src
#define AML_SD_EMMC_FCLK_DIV2_MIN_FREQ       20000000   //20MHz

#define AML_SD_EMMC_MAX_BLK_SIZE              512

//Default values after reset.EMMC Design Docs by AMLOGIC: PG 56
#define AML_SD_EMMC_DEFAULT_BL_LEN           9          //512 bytes
#define AML_SD_EMMC_DEFAULT_RESP_TIMEOUT     8          //256 core clock cycles
#define AML_SD_EMMC_DEFAULT_RC_CC            4          //16 core clock cycles
#define AML_SD_EMMC_DEFAULT_CLK_SRC          0          //24MHz
#define AML_SD_EMMC_DEFAULT_CLK_DIV          60         //Defaults to 400KHz
#define AML_SD_EMMC_DEFAULT_CLK_CORE_PHASE   2
#define AML_SD_EMMC_MAX_TUNING_TRIES         7
#define AML_SD_EMMC_ADJ_DELAY_TEST_ATTEMPTS  10

#define AML_SD_EMMC_DEFAULT_CMD_TIMEOUT     0xc          //2^12 ms.

#define AML_SD_EMMC_SRAM_MEMORY_BASE         0x200
#define AML_SD_EMMC_SRAM_MEMORY_SIZE         512
#define AML_SD_EMMC_PING_BUFFER_BASE         0x400
#define AML_SD_EMMC_PING_BUFFER_SIZE         512
#define AML_SD_EMMC_PONG_BUFER_BASE          0x600
#define AML_SD_EMMC_PONG_BUFFER_SIZE         512
#define AML_SD_EMMC_MAX_PIO_DESCS            32  // 16 * 32 = 512
#define AML_SD_EMMC_MAX_PIO_DATA_SIZE        AML_SD_EMMC_PING_BUFFER_SIZE + \
                                             AML_SD_EMMC_PONG_BUFFER_SIZE

#define AML_SDIO_PORTB_GPIO_REG_5_VAL       0x00020000
#define AML_SDIO_PORTB_PERIPHS_PINMUX2_VAL  0x01000000
#define AML_SDIO_PORTB_PERIPHS_GPIO2_EN     0xffffffef
#define AML_SDIO_PORTB_HHI_GCLK_MPEG0_VAL   0x02000000
#define AML_SDIO_PORTB_SD_EMMC_CLK_VAL      0xf181ffff

static inline void update_bits(uint32_t *x, uint32_t mask, uint32_t loc, uint32_t val) {
    *x &= ~mask;
    *x |= ((val << loc) & mask);
}

static inline uint32_t get_bits(uint32_t x, uint32_t mask, uint32_t loc) {
    return (x & mask) >> loc;
}

static inline bool get_bit(uint32_t x, uint32_t mask) {
    return (x & mask) ? 1 : 0;
}

#define AML_SD_EMMC_CLOCK_OFFSET                0x00
#define AML_SD_EMMC_CLOCK_CFG_DIV_LOC           0
#define AML_SD_EMMC_CLOCK_CFG_DIV_MASK          0x0000003f
#define AML_SD_EMMC_CLOCK_CFG_SRC_LOC           6
#define AML_SD_EMMC_CLOCK_CFG_SRC_MASK          0x000000c0
#define AML_SD_EMMC_CLOCK_CFG_CO_PHASE_LOC      8
#define AML_SD_EMMC_CLOCK_CFG_CO_PHASE_MASK     0x00000300
#define AML_SD_EMMC_CLOCK_CFG_TX_PHASE_LOC      10
#define AML_SD_EMMC_CLOCK_CFG_TX_PHASE_MASK     0x00000c00
#define AML_SD_EMMC_CLOCK_CFG_RX_PHASE_LOC      12
#define AML_SD_EMMC_CLOCK_CFG_RX_PHASE_MASK     0x00003000
#define AML_SD_EMMC_CLOCK_CFG_SRAM_PD_LOC       14
#define AML_SD_EMMC_CLOCK_CFG_SRAM_PD_MASK      0x0000c000
#define AML_SD_EMMC_CLOCK_CFG_TX_DELAY_LOC      16
#define AML_SD_EMMC_CLOCK_CFG_TX_DELAY_MASK     0x003f0000
#define AML_SD_EMMC_CLOCK_CFG_RX_DELAY_LOC      22
#define AML_SD_EMMC_CLOCK_CFG_RX_DELAY_MASK     0x0fc00000
#define AML_SD_EMMC_CLOCK_CFG_ALWAYS_ON         0x10000000
#define AML_SD_EMMC_CLOCK_CFG_IRQ_SDIO_SLEEP    0x20000000
#define AML_SD_EMMC_CLOCK_CFG_IRQ_SDIO_SLEEP_DS 0x40000000
#define AML_SD_EMMC_CLOCK_CFG_NAND              0x80000000

#define AML_SD_EMMC_DELAY1_OFFSET               0x04
#define AML_SD_EMMC_DELAY_DATA0_LOC             0
#define AML_SD_EMMC_DELAY_DATA0_MASK            0x0000003f
#define AML_SD_EMMC_DELAY_DATA1_LOC             6
#define AML_SD_EMMC_DELAY_DATA1_MASK            0x00000fc0
#define AML_SD_EMMC_DELAY_DATA2_LOC             12
#define AML_SD_EMMC_DELAY_DATA2_MASK            0x0003f000
#define AML_SD_EMMC_DELAY_DATA3_LOC             18
#define AML_SD_EMMC_DELAY_DATA3_MASK            0x00fc0000
#define AML_SD_EMMC_DELAY_DATA4_LOC             24
#define AML_SD_EMMC_DELAY_DATA4_MASK            0x3f000000
#define AML_SD_EMMC_DELAY_SPARE_LOC             30
#define AML_SD_EMMC_DELAY_SPARE_MASK            0xc0000000

#define AML_SD_EMMC_DELAY2_OFFSET               0x08
#define AML_SD_EMMC_ADJUST_OFFSET               0x0c
#define AML_SD_EMMC_ADJUST_CALI_SEL_LOC         8
#define AML_SD_EMMC_ADJUST_CALI_SEL_MASK        0x00000f00
#define AML_SD_EMMC_ADJUST_CALI_ENABLE          0x00001000
#define AML_SD_EMMC_ADJUST_ADJ_FIXED            0x00002000
#define AML_SD_EMMC_ADJUST_CALI_RISE            0x00004000
#define AML_SD_EMMC_ADJUST_DS_ENABLE            0x00008000
#define AML_SD_EMMC_ADJUST_ADJ_DELAY_LOC        16
#define AML_SD_EMMC_ADJUST_ADJ_DELAY_MASK       0x003f0000
#define AML_SD_EMMC_ADJUST_ADJ_AUTO             0x00400000

#define AML_SD_EMMC_CALOUT_OFFSET               0x10
#define AML_SD_EMMC_CALOUT_CALI_IDX_LOC         0
#define AML_SD_EMMC_CALOUT_CALI_IDX_MASK        0x0000003f
#define AML_SD_EMMC_CALOUT_CALI_VLD             0x00000040
#define AML_SD_EMMC_CALOUT_CALI_SETUP_LOC       8
#define AML_SD_EMMC_CALOUT_CALI_SETUP_MASK      0x0000ff00

#define AML_SD_EMMC_CALOUTV2_OFFSET             0x14
#define AML_SD_EMMC_CALOUTV2_OFFSET             0x14
#define AML_SD_EMMC_START_OFFSET                0x40
#define AML_SD_EMMC_START_DESC_INT              0x00000001
#define AML_SD_EMMC_START_DESC_BUSY             0x00000002
#define AML_SD_EMMC_START_DESC_ADDR_LOC         2
#define AML_SD_EMMC_START_DESC_ADDR_MASK        0xfffffffc

#define AML_SD_EMMC_CFG_OFFSET                  0x44
#define AML_SD_EMMC_CFG_BUS_WIDTH_LOC           0
#define AML_SD_EMMC_CFG_BUS_WIDTH_MASK          0x00000003
#define AML_SD_EMMC_CFG_BUS_WIDTH_1BIT          0x00000000
#define AML_SD_EMMC_CFG_BUS_WIDTH_4BIT          0x00000001
#define AML_SD_EMMC_CFG_BUS_WIDTH_8BIT          0x00000002
#define AML_SD_EMMC_CFG_DDR                     0x00000004
#define AML_SD_EMMC_CFG_DC_UGT                  0x00000008
#define AML_SD_EMMC_CFG_BL_LEN_LOC              4
#define AML_SD_EMMC_CFG_BL_LEN_MASK             0x000000f0
#define AML_SD_EMMC_CFG_RESP_TIMEOUT_LOC        8
#define AML_SD_EMMC_CFG_RESP_TIMEOUT_MASK       0x00000f00
#define AML_SD_EMMC_CFG_RC_CC_LOC               12
#define AML_SD_EMMC_CFG_RC_CC_MASK              0x0000f000
#define AML_SD_EMMC_CFG_OUT_FALL                0x00010000
#define AML_SD_EMMC_CFG_BLK_GAP_IP              0x00020000
#define AML_SD_EMMC_CFG_SDCLK_ALWAYS_ON         0x00040000
#define AML_SD_EMMC_CFG_IGNORE_OWNER            0x00080000
#define AML_SD_EMMC_CFG_CHK_DS                  0x00100000
#define AML_SD_EMMC_CFG_CMD_LOW                 0x00200000
#define AML_SD_EMMC_CFG_STOP_CLK                0x00400000
#define AML_SD_EMMC_CFG_AUTO_CLK                0x00800000
#define AML_SD_EMMC_CFG_TXD_ADD_ERR             0x01000000
#define AML_SD_EMMC_CFG_TXD_RETRY               0x02000000
#define AML_SD_EMMC_CFG_IRQ_DS                  0x04000000
#define AML_SD_EMMC_CFG_ERR_ABORT               0x08000000
#define AML_SD_EMMC_CFG_IP_TXD_ADJ_LOC          28
#define AML_SD_EMMC_CFG_IP_TXD_ADJ_MASK         0xf0000000

#define AML_SD_EMMC_STATUS_OFFSET               0x48
#define AML_SD_EMMC_STATUS_RXD_ERR_LOC          0
#define AML_SD_EMMC_STATUS_RXD_ERR_MASK         0x000000ff
#define AML_SD_EMMC_STATUS_TXD_ERR              0x00000100
#define AML_SD_EMMC_STATUS_DESC_ERR             0x00000200
#define AML_SD_EMMC_STATUS_RESP_ERR             0x00000400
#define AML_SD_EMMC_STATUS_RESP_TIMEOUT         0x00000800
#define AML_SD_EMMC_STATUS_DESC_TIMEOUT         0x00001000
#define AML_SD_EMMC_STATUS_END_OF_CHAIN         0x00002000
#define AML_SD_EMMC_STATUS_RESP_STATUS          0x00004000
#define AML_SD_EMMC_STATUS_IRQ_SDIO             0x00008000
#define AML_SD_EMMC_STATUS_DAT_I_LOC            16
#define AML_SD_EMMC_STATUS_DAT_I_MASK           0x00ff0000
#define AML_SD_EMMC_STATUS_CMD_I                0x01000000
#define AML_SD_EMMC_STATUS_DS                   0x02000000
#define AML_SD_EMMC_STATUS_BUS_FSM_LOC          26
#define AML_SD_EMMC_STATUS_BUS_FSM_MASK         0x3c000000
#define AML_SD_EMMC_STATUS_BUS_DESC_BUSY        0x40000000
#define AML_SD_EMMC_STATUS_BUS_CORE_BUSY        0x80000000

#define AML_SD_EMMC_IRQ_EN_OFFSET               0x4c
#define AML_SD_EMMC_CMD_CFG_OFFSET              0x50
#define AML_SD_EMMC_CMD_INFO_LEN_LOC            0
#define AML_SD_EMMC_CMD_INFO_LEN_MASK           0x000001ff
#define AML_SD_EMMC_CMD_INFO_BLOCK_MODE         0x00000200
#define AML_SD_EMMC_CMD_INFO_R1B                0x00000400
#define AML_SD_EMMC_CMD_INFO_END_OF_CHAIN       0x00000800
#define AML_SD_EMMC_CMD_INFO_TIMEOUT_LOC        12
#define AML_SD_EMMC_CMD_INFO_TIMEOUT_MASK       0x0000f000
#define AML_SD_EMMC_CMD_INFO_NO_RESP            0x00010000
#define AML_SD_EMMC_CMD_INFO_NO_CMD             0x00020000
#define AML_SD_EMMC_CMD_INFO_DATA_IO            0x00040000
#define AML_SD_EMMC_CMD_INFO_DATA_WR            0x00080000
#define AML_SD_EMMC_CMD_INFO_RESP_NO_CRC        0x00100000
#define AML_SD_EMMC_CMD_INFO_RESP_128           0x00200000
#define AML_SD_EMMC_CMD_INFO_RESP_NUM           0x00400000
#define AML_SD_EMMC_CMD_INFO_DATA_NUM           0x00800000
#define AML_SD_EMMC_CMD_INFO_CMD_IDX_LOC        24
#define AML_SD_EMMC_CMD_INFO_CMD_IDX_MASK       0x3f000000
#define AML_SD_EMMC_CMD_INFO_ERROR              0x40000000
#define AML_SD_EMMC_CMD_INFO_OWNER              0x80000000

#define AML_SD_EMMC_CMD_ARG_OFFSET              0x54
#define AML_SD_EMMC_CMD_DAT_OFFSET              0x58
#define AML_SD_EMMC_CMD_RSP_OFFSET              0x5c
#define AML_SD_EMMC_CMD_RSP1_OFFSET             0x60
#define AML_SD_EMMC_CMD_RSP2_OFFSET             0x64
#define AML_SD_EMMC_CMD_RSP3_OFFSET             0x68
#define AML_SD_EMMC_CMD_BUS_ERR_OFFSET          0x6c
#define AML_SD_EMMC_CURR_CFG_OFFSET             0x70
#define AML_SD_EMMC_CURR_ARG_OFFSET             0x74
#define AML_SD_EMMC_CURR_DAT_OFFSET             0x78
#define AML_SD_EMMC_CURR_RSP_OFFSET             0x7c
#define AML_SD_EMMC_NXT_CFG_OFFSET              0x80
#define AML_SD_EMMC_NXT_ARG_OFFSET              0x84
#define AML_SD_EMMC_NXT_DAT_OFFSET              0x88
#define AML_SD_EMMC_NXT_RSP_OFFSET              0x8c
#define AML_SD_EMMC_RXD_OFFSET                  0x90
#define AML_SD_EMMC_TXD_OFFSET                  0x94
#define AML_SD_EMMC_SRAMDESC_OFFSET             0x200
#define AML_SD_EMMC_PING_OFFSET                 0x400
#define AML_SD_EMMC_PONG_OFFSET                 0x800

typedef struct {
    uint32_t cmd_info;
    uint32_t cmd_arg;
    uint32_t data_addr;
    uint32_t resp_addr;
} aml_sd_emmc_desc_t;

typedef struct {
    bool supports_dma;
    uint32_t min_freq;
    uint32_t max_freq;
} aml_sd_emmc_config_t;

static const uint8_t aml_sd_emmc_tuning_blk_pattern_4bit[64] = {
    0xff, 0x0f, 0xff, 0x00, 0xff, 0xcc, 0xc3, 0xcc,
    0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef,
    0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb,
    0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef,
    0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c,
    0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee,
    0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff,
    0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde,
};

static const uint8_t aml_sd_emmc_tuning_blk_pattern_8bit[128] = {
    0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00,
    0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 0xcc,
    0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff,
    0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 0xff,
    0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd,
    0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 0xbb,
    0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff,
    0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee, 0xff,
    0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00,
    0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc,
    0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff,
    0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee,
    0xff, 0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd,
    0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff,
    0xbb, 0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff,
    0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee,
};
