// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/listnode.h>
#include <zircon/types.h>
#include <threads.h>
#include "edid.h"

#define DW_DSI_READ32(a)        readl(io_buffer_virt(&dsi->mmio) + a)
#define DW_DSI_WRITE32(a, v)    writel(v, io_buffer_virt(&dsi->mmio) + a)

#define DW_DSI_MASK(start, count) (((1 << (count)) - 1) << (start))

#define DW_DSI_SET_BITS32(dest, value, count, start) \
            DW_DSI_WRITE32(dest, (DW_DSI_READ32(dest) & ~DW_DSI_MASK(start, count)) | \
                                (((value) << (start)) & DW_DSI_MASK(start, count)))
#define DW_DSI_SET_MASK(mask, start, count, value) \
                        ((mask & ~DW_DSI_MASK(start, count)) | \
                                (((value) << (start)) & DW_DSI_MASK(start, count)))

#define DW_DSI_VERSION                  0x0     /* contains the vers of the DSI host controller */
#define DW_DSI_PWR_UP                   0x4     /* controls the power up of the core */
#define DW_DSI_CLKMGR_CFG               0x8     /* configs the factor for internal dividers */
#define DW_DSI_DPI_VCID                 0xc     /* configs the Virt Chan ID for DPI traffic */
#define DW_DSI_DPI_COLOR_CODING         0x10    /* configs DPI color coding */
#define DW_DSI_DPI_CFG_POL              0x14    /* configs the polarity of DPI signals */
#define DW_DSI_DPI_LP_CMD_TIM           0x18    /* configs the timing for lp cmds (in vid mode) */
#define DW_DSI_DBI_VCID                 0x1c    /* configs Virtual Channel ID for DBI traffic */
#define DW_DSI_DBI_CFG                  0x20    /* configs the bit width of pixels for DBI */
#define DW_DSI_DBI_PARTITIONING_EN      0x24    /* host partition DBI traffic automatically */
#define DW_DSI_DBI_CMDSIZE              0x28    /* cmd size for auto partitioning of DBI */
#define DW_DSI_PCKHDL_CFG               0x2c    /* how EoTp, BTA, CRC and ECC are to be used */
#define DW_DSI_GEN_VCID                 0x30    /* Virtual Channel ID of READ responses to store */
#define DW_DSI_MODE_CFG                 0x34    /* mode of op between Video or Command Mode */
#define DW_DSI_VID_MODE_CFG             0x38    /* Video mode operation config */
#define DW_DSI_VID_PKT_SIZE             0x3c    /* video packet size */
#define DW_DSI_VID_NUM_CHUNKS           0x40    /* number of chunks to use  */
#define DW_DSI_VID_NULL_SIZE            0x44    /* configs the size of null packets */
#define DW_DSI_VID_HSA_TIME             0x48    /* configs the video HSA time */
#define DW_DSI_VID_HBP_TIME             0x4c    /* configs the video HBP time */
#define DW_DSI_VID_HLINE_TIME           0x50    /* configs the overall time for each video line */
#define DW_DSI_VID_VSA_LINES            0x54    /* configs the VSA period */
#define DW_DSI_VID_VBP_LINES            0x58    /* configs the VBP period */
#define DW_DSI_VID_VFP_LINES            0x5c    /* configs the VFP period */
#define DW_DSI_VID_VACTIVE_LINES        0x60    /* configs the vertical resolution of video */
#define DW_DSI_EDPI_CMD_SIZE            0x64    /* configs the size of eDPI packets */
#define DW_DSI_CMD_MODE_CFG             0x68    /* command mode operation config */
#define DW_DSI_GEN_HDR                  0x6c    /* header for new packets */
#define DW_DSI_GEN_PLD_DATA             0x70    /* payload for packets sent using the Gen i/f */
#define DW_DSI_CMD_PKT_STATUS           0x74    /* info about FIFOs related to DBI and Gen i/f */
#define DW_DSI_TO_CNT_CFG               0x78    /* counters that trig timeout errors */
#define DW_DSI_HS_RD_TO_CNT             0x7c    /* Peri Resp timeout after HS Rd operations */
#define DW_DSI_LP_RD_TO_CNT             0x80    /* Peri Resp timeout after LP Rd operations */
#define DW_DSI_HS_WR_TO_CNT             0x84    /* Peri Resp timeout after HS Wr operations */
#define DW_DSI_LP_WR_TO_CNT             0x88    /* Peri Resp timeout after LP Wr operations */
#define DW_DSI_BTA_TO_CNT               0x8c    /* Peri Resp timeout after Bus Turnaround comp */
#define DW_DSI_SDF_3D                   0x90    /* 3D cntrl info for VSS packets in video mode. */
#define DW_DSI_LPCLK_CTRL               0x94    /* non continuous clock in the clock lane. */
#define DW_DSI_PHY_TMR_LPCLK_CFG        0x98    /* time for the clock lane  */
#define DW_DSI_PHY_TMR_CFG              0x9c    /* time for the data lanes  */
#define DW_DSI_PHY_RSTZ                 0xa0    /* controls resets and the PLL of the D-PHY. */
#define DW_DSI_PHY_IF_CFG               0xa4    /* number of active lanes  */
#define DW_DSI_PHY_ULPS_CTRL            0xa8    /* entering and leaving ULPS in the D- PHY. */
#define DW_DSI_PHY_TX_TRIGGERS          0xac    /* pins that activate triggers in the D-PHY */
#define DW_DSI_PHY_STATUS               0xb0    /* contains info about the status of the D- PHY */
#define DW_DSI_PHY_TST_CTRL0            0xb4    /* controls clock and clear pins of the D-PHY */
#define DW_DSI_PHY_TST_CTRL1            0xb8    /* controls data and enable pins of the D-PHY */
#define DW_DSI_INT_ST0                  0xbc    /* status of intr from ack and D-PHY */
#define DW_DSI_INT_ST1                  0xc0    /* status of intr related to timeout, ECC, etc */
#define DW_DSI_INT_MSK0                 0xc4    /* masks interrupts that affect the INT_ST0 reg */
#define DW_DSI_INT_MSK1                 0xc8    /* masks interrupts that affect the INT_ST1 reg */
#define DW_DSI_PHY_CAL                  0xcc    /* controls the skew calibration of D-PHY. */
#define DW_DSI_INT_FORCE0               0xd8    /* forces that affect the INT_ST0 register. */
#define DW_DSI_INT_FORCE1               0xdc    /* forces interrupts that affect the INT_ST1 reg */
#define DW_DSI_DSC_PARAMETER            0xf0    /* configs Display Stream Compression */
#define DW_DSI_PHY_TMR_RD_CFG           0xf4    /* PHY related times for ops in lane byte clock */
#define DW_DSI_VID_SHADOW_CTRL          0x100   /* controls dpi shadow feature */
#define DW_DSI_DPI_VCID_ACT             0x10c   /* val used for DPI_VCID. */
#define DW_DSI_DPI_COLOR_CODING_ACT     0x110   /* val used for DPI_COLOR_CODING. */
#define DW_DSI_DPI_LP_CMD_TIM_ACT       0x118   /* val used for DPI_LP_CMD_TIM. */
#define DW_DSI_VID_MODE_CFG_ACT         0x138   /* val used for VID_MODE_CFG.*/
#define DW_DSI_VID_PKT_SIZE_ACT         0x13c   /* val used for VID_PKT_SIZE.*/
#define DW_DSI_VID_NUM_CHUNKS_ACT       0x140   /* val used for VID_NUM_CHUNKS.*/
#define DW_DSI_VID_NULL_SIZE_ACT        0x144   /* val used for VID_NULL_SIZE.*/
#define DW_DSI_VID_HSA_TIME_ACT         0x148   /* val used for VID_HSA_TIME.*/
#define DW_DSI_VID_HBP_TIME_ACT         0x14c   /* val used for VID_HBP_TIME.*/
#define DW_DSI_VID_HLINE_TIME_ACT       0x150   /* val used for VID_HLINE_TIME.*/
#define DW_DSI_VID_VSA_LINES_ACT        0x154   /* val used for VID_VSA_LINES.*/
#define DW_DSI_VID_VBP_LINES_ACT        0x158   /* val used for VID_VBP_LINES.*/
#define DW_DSI_VID_VFP_LINES_ACT        0x15c   /* val used for VID_VFP_LINES.*/
#define DW_DSI_VID_VACTIVE_LINES_ACT    0x160   /* val used for VID_VACTIVE_LINES.*/
#define DW_DSI_SDF_3D_ACT               0x190   /* val used for SDF_3D.*/


/* Bit DPI_CFG_POL Definitions */
#define DW_DSI_DPI_CFG_POL_DATAEN_START      (0)
#define DW_DSI_DPI_CFG_POL_VSYNC_START       (1)
#define DW_DSI_DPI_CFG_POL_HSYNC_START       (2)
#define DW_DSI_DPI_CFG_POL_SHUTD_START       (3)
#define DW_DSI_DPI_CFG_POL_COLORM_START      (4)

/* Bit VID_MODE_CFG Definitions */
#define DW_DSI_VID_MODE_CFG_LP_CMD_START      (15)
#define DW_DSI_VID_MODE_CFG_LP_CMD_BITS       (1)
#define DW_DSI_VID_MODE_CFG_FRAME_ACK_START      (14)
#define DW_DSI_VID_MODE_CFG_FRAME_ACK_BITS       (1)
#define DW_DSI_VID_MODE_CFG_LP_ALL_START       (8)
#define DW_DSI_VID_MODE_CFG_LP_ALL_BITS        (6)
#define DW_DSI_VID_MODE_CFG_LP_VSA     (1 << 8)
#define DW_DSI_VID_MODE_CFG_LP_VBP     (1 << 9)
#define DW_DSI_VID_MODE_CFG_LP_VFP     (1 << 10)
#define DW_DSI_VID_MODE_CFG_LP_VACT    (1 << 11)
#define DW_DSI_VID_MODE_CFG_LP_HBP     (1 << 12)
#define DW_DSI_VID_MODE_CFG_LP_HFP     (1 << 13)
#define DW_DSI_VID_MODE_CFG_ALL_LP (DW_DSI_VID_MODE_CFG_LP_VSA | \
                                    DW_DSI_VID_MODE_CFG_LP_VBP  | \
                                    DW_DSI_VID_MODE_CFG_LP_VFP  | \
                                    DW_DSI_VID_MODE_CFG_LP_VACT | \
                                    DW_DSI_VID_MODE_CFG_LP_HBP  |\
                                    DW_DSI_VID_MODE_CFG_LP_HFP)
#define DW_DSI_VID_MODE_CFG_MODE_START       (0)
#define DW_DSI_VID_MODE_CFG_MODE_BITS        (2)

/* Bit VID_PKT_SIZE Definitions */
#define DW_DSI_VID_PKT_SIZE_START          0
#define DW_DSI_VID_PKT_SIZE_BITS           14

/* Bit PHY_RSTZ Definitions */
#define DW_DSI_PHY_RSTZ_SHUTDOWN   (0)
#define DW_DSI_PHY_RSTZ_ENABLE     (0x7)

/* Bit HY_STATUS Definitions */
#define DW_DSI_PHY_STATUS_PHY_LOCKED   (1 << 0)
#define DW_DSI_PHY_STATUS_L0STOP       (1 << 4)
#define DW_DSI_PHY_STATUS_LxSTOP(x)    (((x + 2) << 1) + 1)
#define DW_DSI_PHY_STATUS_ALLSTOP      (0xA90)
#define DW_DSI_PHY_STATUS_
#define DW_DSI_PHY_TST_CTRL0_TSTCLK (1 << 1)
#define DW_DSI_PHY_TST_CTRL0_TSTCLR (0 << 0)
#define DW_DSI_PHY_TST_CTRL1_TESTEN (1 << 16)


#define DS_NUM_LANES                        4
#define DSI_COLOR_CODE_24BITS               0x5
#define DSI_CFG_POL_ACTIVE_HIGH             0
#define DSI_CFG_POL_ACTIVE_LOW              1
#define DSI_NON_BURST_SYNC_PULSES           0

#define LANE_BYTE_CLOCK (108000000ULL)
#define ROUND(x, y)     ((x) / (y) + \
                ((x) % (y) * 10 / (y) >= 5 ? 1 : 0))
#define ROUND1(x, y)    ((x) / (y) + ((x) % (y)  ? 1 : 0))

typedef enum {
    I2C_MAIN,
    I2C_CEC,
    I2C_EDID,
} adv7533_if_t;

typedef enum {
    GPIO_MUX,
    GPIO_PD,
    GPIO_INT,
} hdmi_gpio_if_t;

typedef struct {
    zx_device_t* zdev;
    i2c_protocol_t i2c;
} adv7533_i2c_t;


typedef struct {
    zx_device_t* zxdev;
    gpio_protocol_t gpio;
} hdmi_gpio_t;


typedef struct {
    zx_device_t*                        zxdev;
    platform_device_protocol_t          pdev;
    zx_device_t*                        parent;
    io_buffer_t                         mmio;

    adv7533_i2c_t                       i2c_dev;        // ADV7533 I2C device
    hdmi_gpio_t                         hdmi_gpio;      // ADV7533-related GPIOs

    char                                write_buf[64];  // scratch buffer used for the i2c driver

    detailed_timing_t*                  std_raw_dtd;
    disp_timing_t*                      std_disp_timing;
    detailed_timing_t*                  raw_dtd;
    disp_timing_t*                      disp_timing;
} dsi_t;

static zx_status_t dsi_mipi_init(dsi_t* dsi);