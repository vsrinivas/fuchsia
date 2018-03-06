// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define I2C_DW_COMP_TYPE_NUM    0x44570140
#define I2C_DW_MAX_TRANSFER     64 // Local buffer for transfer and recieve. Matches FIFO size
#define I2C_ERROR_SIGNAL        ZX_USER_SIGNAL_0
#define I2C_TXN_COMPLETE_SIGNAL ZX_USER_SIGNAL_1

#define I2C_DW_READ32(a)        readl(dev->virt_reg + a)
#define I2C_DW_WRITE32(a, v)    writel(v, dev->virt_reg + a)

#define I2C_DW_MASK(start, count) (((1 << (count)) - 1) << (start))
#define I2C_DW_GET_BITS32(src, start, count) ((I2C_DW_READ32(src) & I2C_DW_MASK(start, count)) >> (start))
#define I2C_DW_SET_BITS32(dest, start, count, value) \
            I2C_DW_WRITE32(dest, (I2C_DW_READ32(dest) & ~I2C_DW_MASK(start, count)) | \
                                (((value) << (start)) & I2C_DW_MASK(start, count)))
#define I2C_DW_SET_MASK(mask, start, count, value) \
                        ((mask & ~I2C_DW_MASK(start, count)) | \
                                (((value) << (start)) & I2C_DW_MASK(start, count)))

#define I2C_DISABLE         0
#define I2C_ENABLE          1
#define I2C_STD_MODE        1
#define I2C_FAST_MODE       2
#define I2C_HS_MODE         3
#define I2C_7BIT_ADDR       0
#define I2C_10BIT_ADDR      0
#define I2C_ACTIVE          1

/* DesignWare I2C Resiter Offset*/
#define DW_I2C_CON                                  0x0     /* I2C Control */
#define DW_I2C_TAR                                  0x4     /* I2C Target Address */
#define DW_I2C_SAR                                  0x8     /* I2C Slave Address */
#define DW_I2C_HS_MADDR                             0xc     /* I2C HS Master Mode Code Address */
#define DW_I2C_DATA_CMD                             0x10    /* I2C Rx/Tx Data Buffer and Command */
#define DW_I2C_SS_SCL_HCNT                          0x14    /* SS I2C Clock SCL High Count */
#define DW_I2C_UFM_SCL_HCNT                         0x14    /* UFS I2C Clock SCL High Count */
#define DW_I2C_SS_SCL_LCNT                          0x18    /* SS I2C Clock SCL Low Count */
#define DW_I2C_UFM_SCL_LCNT                         0x18    /* UFS I2C Clock SCL Low Count */
#define DW_I2C_FS_SCL_HCNT                          0x1c    /* Fast Mode I2C Clock SCL High Cnt */
#define DW_I2C_UFM_TBUF_CNT                         0x1c    /* UFS mode TBuf Idle Count */
#define DW_I2C_FS_SCL_LCNT                          0x20    /* Fast Mode I2C Clock SCL Low Cnt */
#define DW_I2C_HS_SCL_HCNT                          0x24    /* High Speed I2C Clock SCL High Cnt */
#define DW_I2C_HS_SCL_LCNT                          0x28    /* High Speed I2C Clock SCL Low Cnt */
#define DW_I2C_INTR_STAT                            0x2c    /* I2C Interrupt Status */
#define DW_I2C_INTR_MASK                            0x30    /* I2C Interrupt Mask */
#define DW_I2C_RAW_INTR_STAT                        0x34    /* I2C Raw Interrupt Status */
#define DW_I2C_RX_TL                                0x38    /* I2C Receive FIFO Threshold */
#define DW_I2C_TX_TL                                0x3c    /* I2C Transmit FIFO Threshold */
#define DW_I2C_CLR_INTR                             0x40    /* Clear Combined and Individual Intr */
#define DW_I2C_CLR_RX_UNDER                         0x44    /* Clear RX_UNDER Interrupt */
#define DW_I2C_CLR_RX_OVER                          0x48    /* Clear RX_OVER Interrupt */
#define DW_I2C_CLR_TX_OVER                          0x4c    /* Clear TX_OVER Interrupt */
#define DW_I2C_CLR_RD_REQ                           0x50    /* Clear RD_REQ Interrupt */
#define DW_I2C_CLR_TX_ABRT                          0x54    /* Clear TX_ABRT Interrupt */
#define DW_I2C_CLR_RX_DONE                          0x58    /* Clear RX_DONE Interrupt */
#define DW_I2C_CLR_ACTIVITY                         0x5c    /* Clear ACTIVITY Interrupt */
#define DW_I2C_CLR_STOP_DET                         0x60    /* Clear STOP_DET Interrupt */
#define DW_I2C_CLR_START_DET                        0x64    /* Clear START_DET Interrupt */
#define DW_I2C_CLR_GEN_CALL                         0x68    /* Clear GEN_CALL Interrupt */
#define DW_I2C_ENABLE                               0x6c    /* I2C Enable */
#define DW_I2C_STATUS                               0x70    /* I2C Status */
#define DW_I2C_TXFLR                                0x74    /* I2C Transmit FIFO Level */
#define DW_I2C_RXFLR                                0x78    /* I2C Receive FIFO Level */
#define DW_I2C_SDA_HOLD                             0x7c    /* I2C SDA Hold Time Length */
#define DW_I2C_TX_ABRT_SOURCE                       0x80    /* I2C Transmit Abort Source */
#define DW_I2C_SLV_DATA_NACK_ONLY                   0x84    /* Generate Slave Data NACK */
#define DW_I2C_DMA_CR                               0x88    /* DMA Control */
#define DW_I2C_DMA_TDLR                             0x8c    /* DMA Transmit Data Level */
#define DW_I2C_DMA_RDLR                             0x90    /* I2C Receive Data Level */
#define DW_I2C_SDA_SETUP                            0x94    /* I2C SDA Setup */
#define DW_I2C_ACK_GENERAL_CALL                     0x98    /* I2C ACK General Call */
#define DW_I2C_ENABLE_STATUS                        0x9c    /* I2C Enable Status */
#define DW_I2C_FS_SPKLEN                            0xa0    /* I2C SS, FS spike suppression limit */
#define DW_I2C_UFM_SPKLEN                           0xa0    /* I2C UFM spike suppression limit */
#define DW_I2C_HS_SPKLEN                            0xa4    /* I2C HS spike suppression limit */
#define DW_I2C_CLR_RESTART_DET                      0xa8    /* Clear RESTART_DET Interrupt */
#define DW_I2C_SCL_STUCK_AT_LOW_TIMEOUT             0xac    /* I2C SCL Stuck at Low Timeout */
#define DW_I2C_SDA_STUCK_AT_LOW_TIMEOUT             0xb0    /* I2C SDA Stuck at Low Timeout */
#define DW_I2C_CLR_SCL_STUCK_DET                    0xb4    /* Clear SCL Stuck at Low Detect Intr */
#define DW_I2C_DEVICE_ID                            0xb8    /* I2C Device-ID */
#define DW_I2C_SMBUS_CLK_LOW_SEXT                   0xbc    /* SMBus Slave Clock Extend Timeout */
#define DW_I2C_SMBUS_CLK_LOW_MEXT                   0xc0    /* SMBus Master Clock Extend Timeout */
#define DW_I2C_SMBUS_THIGH_MAX_IDLE_COUNT           0xc4    /* SMBus Master High MAX Bus-idle cnt */
#define DW_I2C_SMBUS_INTR_STAT                      0xc8    /* SMBUS Interrupt Status */
#define DW_I2C_SMBUS_INTR_MASK                      0xcc    /* SMBus Interrupt Mask */
#define DW_I2C_SMBUS_RAW_INTR_STAT                  0xd0    /* SMBus Raw Interrupt Status */
#define DW_I2C_CLR_SMBUS_INTR                       0xd4    /* SMBus Clear Interrupt */
#define DW_I2C_OPTIONAL_SAR                         0xd8    /* I2C Optional Slave Address */
#define DW_I2C_SMBUS_UDID_LSB                       0xdc    /* SMBUS ARP UDID LSB */
#define DW_I2C_COMP_PARAM_1                         0xf4    /* Component Parameter */
#define DW_I2C_COMP_VERSION                         0xf8    /* I2C Component Version */
#define DW_I2C_COMP_TYPE                            0xfc


/* DW_I2C_CON Bit Definitions */
#define DW_I2C_CON_MASTER_MODE_START                0
#define DW_I2C_CON_MASTER_MODE_BITS                 1
#define DW_I2C_CON_SPEED_START                      1
#define DW_I2C_CON_SPEED_BITS                       2
#define DW_I2C_CON_10BITADDRSLAVE_START             3
#define DW_I2C_CON_10BITADDRSLAVE_BITS              1
#define DW_I2C_CON_10BITADDRMASTER_START            4
#define DW_I2C_CON_10BITADDRMASTER_BITS             1
#define DW_I2C_CON_RESTART_EN_START                 5
#define DW_I2C_CON_RESTART_EN_BITS                  1
#define DW_I2C_CON_SLAVE_DIS_START                  6
#define DW_I2C_CON_SLAVE_DIS_BITS                   1
#define DW_I2C_CON_TX_EMPTY_CTRL_START              8
#define DW_I2C_CON_TX_EMPTY_CTRL_BITS               1

/* DW_I2C_TAR Bit Definitions */
#define DW_I2C_TAR_TAR_START                        0
#define DW_I2C_TAR_TAR_BITS                         10
#define DW_I2C_TAR_10BIT_START                      12
#define DW_I2C_TAR_10BIT_BITS                       1

/* DW_I2C_DATA_CMD_DAT Bit Definitions */
#define DW_I2C_DATA_CMD_DAT_START                   0
#define DW_I2C_DATA_CMD_DAT_BITS                    8
#define DW_I2C_DATA_CMD_CMD_START                   8
#define DW_I2C_DATA_CMD_CMD_BITS                    1
#define DW_I2C_DATA_CMD_STOP_START                  9
#define DW_I2C_DATA_CMD_STOP_BITS                   1
#define DW_I2C_DATA_CMD_RESTART_START               10
#define DW_I2C_DATA_CMD_RESTART_BITS                1
#define DW_I2C_DATA_CMD_FRST_DAT_BYTE_START         11
#define DW_I2C_DATA_CMD_FRST_DAT_BYTE_BITS          1

/* DW_I2C_SS/FS_SCL Bit Definitions */
#define DW_I2C_SS_SCL_HCNT_START                    0
#define DW_I2C_SS_SCL_HCNT_BITS                     16
#define DW_I2C_SS_SCL_LCNT_START                    0
#define DW_I2C_SS_SCL_LCNT_BITS                     16
#define DW_I2C_FS_SCL_HCNT_START                    0
#define DW_I2C_FS_SCL_HCNT_BITS                     16
#define DW_I2C_FS_SCL_LCNT_START                    0
#define DW_I2C_FS_SCL_LCNT_BITS                     16

/* DW_I2C_INTR Bit Definitions */
#define DW_I2C_INTR_SCL_STUCK_LOW                   (0x4000)
#define DW_I2C_INTR_MSTR_ON_HOLD                    (0x2000)
#define DW_I2C_INTR_RESTART_DET                     (0x1000)
#define DW_I2C_INTR_GEN_CALL                        (0x0800)
#define DW_I2C_INTR_START_DET                       (0x0400)
#define DW_I2C_INTR_STOP_DET                        (0x0200)
#define DW_I2C_INTR_ACTIVITY                        (0x0100)
#define DW_I2C_INTR_RX_DONE                         (0x0080)
#define DW_I2C_INTR_TX_ABRT                         (0x0040)
#define DW_I2C_INTR_RD_REQ                          (0x0020)
#define DW_I2C_INTR_TX_EMPTY                        (0x0010)
#define DW_I2C_INTR_TX_OVER                         (0x0008)
#define DW_I2C_INTR_RX_FULL                         (0x0004)
#define DW_I2C_INTR_RX_OVER                         (0x0002)
#define DW_I2C_INTR_RX_UNDER                        (0x0001)
#define DW_I2C_INTR_DEFAULT_INTR_MASK               (DW_I2C_INTR_RX_FULL | \
                                                    DW_I2C_INTR_TX_ABRT | \
                                                    DW_I2C_INTR_STOP_DET | \
                                                    DW_I2C_INTR_TX_EMPTY)

#define DW_I2C_INTR_READ_INTR_MASK                  (DW_I2C_INTR_RX_FULL | \
                                                    DW_I2C_INTR_TX_ABRT | \
                                                    DW_I2C_INTR_STOP_DET )

/* DW_I2C_RX/TX_TL Bit Definitions */
#define DW_I2C_RX_TL_START                          0
#define DW_I2C_RX_TL_BITS                           8
#define DW_I2C_TX_TL_START                          0
#define DW_I2C_TX_TL_BITS                           8

/* DW_I2C_ENABLE Bit Definitions */
#define DW_I2C_ENABLE_ENABLE_START                  0
#define DW_I2C_ENABLE_ENABLE_BITS                   1

/* DW_I2C_STATUS Bit Definitions */
#define DW_I2C_STATUS_ACTIVITY_START                0
#define DW_I2C_STATUS_ACTIVITY_BITS                 1

/* DW_I2C_ENABLE_STATUS Bit Definitions */
#define DW_I2C_ENABLE_STATUS_EN_START               0
#define DW_I2C_ENABLE_STATUS_EN_BITS                1

/* DW_I2C_COMP_PARAM_1 Bit Definitions */
#define DW_I2C_COMP_PARAM_1_RXFIFOSZ_START          8
#define DW_I2C_COMP_PARAM_1_RXFIFOSZ_BITS           8
#define DW_I2C_COMP_PARAM_1_TXFIFOSZ_START          16
#define DW_I2C_COMP_PARAM_1_TXFIFOSZ_BITS           8
