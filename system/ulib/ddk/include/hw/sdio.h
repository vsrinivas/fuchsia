// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <zircon/compiler.h>
#include "sdmmc.h"


#define SDIO_IO_RW_DIRECT             52
#define SDIO_IO_RW_DIRECT_EXTENDED    53
#define SDIO_SEND_OP_COND             5

#define SDIO_IO_RW_DIRECT_FLAGS             SDMMC_RESP_R5 | SDMMC_CMD_TYPE_ABORT
#define SDIO_IO_RW_DIRECT_EXTENDED_FLAGS    SDMMC_RESP_R5 | SDMMC_CMD_TYPE_ABORT | \
                                            SDMMC_RESP_DATA_PRESENT
#define SDIO_SEND_OP_COND_FLAGS             SDMMC_RESP_R4

//(CMD5) Fields
#define SDIO_SEND_OP_COND_IO_OCR_33V           (1 << 21)
#define SDIO_SEND_OP_COND_CMD_S18R             (1 << 24)

//CMD5 RESP OCR Fields
#define SDIO_SEND_OP_COND_RESP_S18A            (1 << 24)
#define SDIO_SEND_OP_COND_RESP_MEM_PRESENT     (1 << 27)
#define SDIO_SEND_OP_COND_RESP_NUM_FUNC_LOC    28
#define SDIO_SEND_OP_COND_RESP_NUM_FUNC_MASK   0x70000000

// IO_RW_DIRECT CMD Fields
#define SDIO_IO_RW_DIRECT_WRITE_BYTE_LOC       0
#define SDIO_IO_RW_DIRECT_WRITE_BYTE_MASK      0x000000ff
#define SDIO_IO_RW_DIRECT_REG_ADDR_LOC         9
#define SDIO_IO_RW_DIRECT_REG_ADDR_MASK        0x03fffe00
#define SDIO_IO_RW_DIRECT_RAW_FLAG             0x08000000
#define SDIO_IO_RW_DIRECT_FN_IDX_LOC           28
#define SDIO_IO_RW_DIRECT_FN_IDX_MASK          0x70000000
#define SDIO_IO_RW_DIRECT_RW_FLAG              0x80000000

// IO_RW_DIRECT RESP Fields
#define SDIO_IO_RW_DIRECT_RESP_READ_BYTE_LOC   0
#define SDIO_IO_RW_DIRECT_RESP_READ_BYTE_MASK  0x000000ff

// IO_RW_EXTENDED Fields
#define SDIO_IO_RW_EXTD_BYTE_BLK_COUNT_LOC      0
#define SDIO_IO_RW_EXTD_BYTE_BLK_COUNT_MASK     0x000001ff
#define SDIO_IO_RW_EXTD_MAX_BLKS_PER_CMD        511 // 9 bits
#define SDIO_IO_RW_EXTD_REG_ADDR_LOC            9
#define SDIO_IO_RW_EXTD_REG_ADDR_MASK           0x03fffe00
#define SDIO_IO_RW_EXTD_OP_CODE_INCR            0x04000000
#define SDIO_IO_RW_EXTD_BLOCK_MODE              0x08000000
#define SDIO_IO_RW_EXTD_FN_IDX_LOC              28
#define SDIO_IO_RW_EXTD_FN_IDX_MASK             0x70000000
#define SDIO_IO_RW_EXTD_RW_FLAG                 0x80000000

//SDIO CIA Fields.Refer Sec 6.8 SDIO SPEC
#define SDIO_CIA_CCCR_CCCR_SDIO_VER_ADDR        0x00
#define SDIO_CIA_CCCR_CCCR_VER_LOC              0
#define SDIO_CIA_CCCR_CCCR_VER_MASK             0x0f
#define SDIO_CIA_CCCR_SDIO_VER_LOC              4
#define SDIO_CIA_CCCR_SDIO_VER_MASK             0xf0

#define SDIO_CCCR_FORMAT_VER_1                  0
#define SDIO_CCCR_FORMAT_VER_1_1                1
#define SDIO_CCCR_FORMAT_VER_2                  2
#define SDIO_CCCR_FORMAT_VER_3                  3
#define SDIO_SDIO_VER_1                         0
#define SDIO_SDIO_VER_1_1                       1
#define SDIO_SDIO_VER_1_2                       2
#define SDIO_SDIO_VER_2                         3
#define SDIO_SDIO_VER_3                         4

#define SDIO_CIA_CCCR_NON_VENDOR_REG_SIZE       0x16
#define SDIO_CIA_CCCR_SD_FORMAT_VER_ADDR        0x01
#define SDIO_CIA_CCCR_IOEx_EN_FUNC_ADDR         0x02
#define SDIO_CIA_CCCR_IORx_FUNC_RDY_ADDR        0x03

#define SDIO_CIA_CCCR_IEN_INTR_EN_ADDR          0x04
#define SDIO_ALL_INTR_ENABLED_MASK              0xFE

#define SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR        0x05
#define SDIO_CIA_CCCR_ASx_ABORT_SEL_CR_ADDR     0x06

#define SDIO_CIA_CCCR_BUS_INTF_CTRL_ADDR        0x07
#define SDIO_CIA_CCCR_INTF_CTRL_BW_LOC          0
#define SDIO_CIA_CCCR_INTF_CTRL_BW_MASK         0x03
#define SDIO_BW_1BIT                            0
#define SDIO_BW_RSVD                            1
#define SDIO_BW_4BIT                            2
#define SDIO_CIA_CCCR_INTF_CTRL_BW_8BIT_SUPPRT  0x04
#define SDIO_CIA_CCCR_INTF_CTRL_CD_DISABLE      0x80

#define SDIO_CIA_CCCR_CARD_CAPS_ADDR            0x08
#define SDIO_CIA_CCCR_CARD_CAP_SDC              0x01
#define SDIO_CIA_CCCR_CARD_CAP_SMB              0x02
#define SDIO_CIA_CCCR_CARD_CAP_SRW              0x04
#define SDIO_CIA_CCCR_CARD_CAP_SBS              0x08
#define SDIO_CIA_CCCR_CARD_CAP_S4MI             0x10
#define SDIO_CIA_CCCR_CARD_CAP_E4MI             0x20
#define SDIO_CIA_CCCR_CARD_CAP_LSC              0x40
#define SDIO_CIA_CCCR_CARD_CAP_4BLS             0x80

#define SDIO_CIA_CCCR_COMMON_CIS_ADDR           0x09    // 0x09 - 0x0B
#define SDIO_CIS_ADDRESS_SIZE                   3       //bytes
#define SDIO_CIA_CCCR_BUS_SUSPEND_ADDR          0x0C
#define SDIO_CIA_CCCR_FUNC_SEL_ADDR             0x0D
#define SDIO_CIA_CCCR_EXEC_FLAGS_ADDR           0x0E
#define SDIO_CIA_CCCR_RDY_FLAGS_ADDR            0x0F
#define SDIO_CIA_CCCR_FN0_BLKSIZE_ADDR          0x10

#define SDIO_CIA_CCCR_PWR_CTRL_ADDR             0x12
#define SDIO_CIA_CCCR_PWR_CTRL_SMPC             0x01
#define SDIO_CIA_CCCR_PWR_CTRL_EMPC             0x02

#define SDIO_CIA_CCCR_BUS_SPEED_SEL_ADDR        0x13
#define SDIO_CIA_CCCR_BUS_SPEED_SEL_SHS         0x01
#define SDIO_CIA_CCCR_BUS_SPEED_BSS_LOC         1
#define SDIO_CIA_CCCR_BUS_SPEED_BSS_MASK        0x0e

#define SDIO_BUS_SPEED_SDR12                    0
#define SDIO_BUS_SPEED_SDR25                    1
#define SDIO_BUS_SPEED_SDR50                    2
#define SDIO_BUS_SPEED_SDR104                   3
#define SDIO_BUS_SPEED_DDR50                    4
#define SDIO_BUS_SPEED_EN_HS                    1

#define SDIO_CIA_CCCR_UHS_SUPPORT_ADDR          0x14
#define SDIO_CIA_CCCR_UHS_SDR50                 0x01
#define SDIO_CIA_CCCR_UHS_SDR104                0x02
#define SDIO_CIA_CCCR_UHS_DDR50                 0x04

#define SDIO_UHS_SDR104_MAX_FREQ                208000000
#define SDIO_UHS_SDR50_MAX_FREQ                 100000000
#define SDIO_UHS_DDR50_MAX_FREQ                 50000000
#define SDIO_HS_MAX_FREQ                        50000000
#define SDIO_DEFAULT_FREQ                       25000000

#define SDIO_CIA_CCCR_DRV_STRENGTH_ADDR         0x15
#define SDIO_CIA_CCCR_DRV_STRENGTH_SDTA         0x01
#define SDIO_CIA_CCCR_DRV_STRENGTH_SDTB         0x02
#define SDIO_CIA_CCCR_DRV_STRENGTH_SDTD         0x04
#define SDIO_CIA_CCCR_DRV_STRENGTH_DTS_LOC      4
#define SDIO_CIA_CCCR_DRV_STRENGTH_DTS_MASK     0x30
#define SDIO_DRV_STRENGTH_TYPE_B                0
#define SDIO_DRV_STRENGTH_TYPE_A                1
#define SDIO_DRV_STRENGTH_TYPE_C                2
#define SDIO_DRV_STRENGTH_TYPE_D                3

#define SDIO_CIA_FBR_BASE_ADDR(f)               ((f) * 0x100)
#define SDIO_CIA_FBR_STD_IF_CODE_ADDR           0x00
#define SDIO_CIA_FBR_STD_IF_CODE_LOC            0
#define SDIO_CIA_FBR_STD_IF_CODE_MASK           0x0f
#define SDIO_CIA_FBR_STD_IF_CODE_EXT_ADDR       0x01
#define SDIO_CIA_FBR_CIS_ADDR                   0x09
#define SDIO_CIA_FBR_BLK_SIZE_ADDR              0x10

//Sec.16.5 CIS FIELDS

#define SDIO_CIS_TPL_FRMT_TCODE_OFF           0x00
#define SDIO_CIS_TPL_FRMT_TLINK_OFF           0x01
#define SDIO_CIS_TPL_FRMT_TBODY_OFF           0x02

#define SDIO_CIS_TPL_CODE_NULL                0x00
#define SDIO_CIS_TPL_CODE_CHECKSUM            0x10
#define SDIO_CIS_TPL_CODE_VES_1               0x15
#define SDIO_CIS_TPL_CODE_ALTSTR              0x16
#define SDIO_CIS_TPL_CODE_MANFID              0x20
#define SDIO_CIS_TPL_MANFID_MIN_BDY_SZ        4
#define SDIO_CIS_TPL_CODE_FUNCID              0x21
#define SDIO_CIS_TPL_CODE_FUNCE               0x22
#define SDIO_CIS_TPL_FUNC0_FUNCE_MIN_BDY_SZ   4
#define SDIO_CIS_TPL_FUNCx_FUNCE_MIN_BDY_SZ   42
#define SDIO_CIS_TPL_CODE_VENDOR_START        0x80
#define SDIO_CIS_TPL_CODE_VENDOR_END          0x8F
#define SDIO_CIS_TPL_CODE_SDIO_STD            0x91
#define SDIO_CIS_TPL_CODE_SDIO_EXT            0x92
#define SDIO_CIS_TPL_CODE_END                 0xFF
#define SDIO_CIS_TPL_LINK_END                 0xFF

#define SDIO_CIS_TPL_FUNCE_FUNC0_MAX_BLK_SIZE_LOC    1
#define SDIO_CIS_TPL_FUNCE_FUNCx_MAX_BLK_SIZE_LOC    12
#define SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_UNIT_LOC   0
#define SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_UNIT_MASK  0x07
#define SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_VAL_LOC    3
#define SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_VAL_MASK   0x78

//Sec.16.7.3
static const uint32_t sdio_cis_tpl_funce_tran_speed_val[16] =
    {0, 100, 120, 130, 150, 200, 250, 300, 350, 400, 450, 500, 550, 600, 700, 800};
static const uint32_t sdio_cis_tpl_funce_tran_speed_unit[8] =
    {1, 10, 100, 1000, 0, 0, 0, 0 }; //Kbit/sec

