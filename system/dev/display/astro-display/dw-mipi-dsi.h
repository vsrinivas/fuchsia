// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//////////////////////////////////////////////////
// DesignWare MIPI DSI Register Definitions
//////////////////////////////////////////////////
#define DW_DSI_VERSION                  (0x00 << 2) // contains the vers of the DSI host controller
#define DW_DSI_PWR_UP                   (0x01 << 2) // controls the power up of the core
#define DW_DSI_CLKMGR_CFG               (0x02 << 2) // configs the factor for internal dividers
#define DW_DSI_DPI_VCID                 (0x03 << 2) // configs the Virt Chan ID for DPI traffic
#define DW_DSI_DPI_COLOR_CODING         (0x04 << 2) // configs DPI color coding
#define DW_DSI_DPI_CFG_POL              (0x05 << 2) // configs the polarity of DPI signals
#define DW_DSI_DPI_LP_CMD_TIM           (0x06 << 2) // configs the timing for lp cmds (in vid mode)
#define DW_DSI_DBI_VCID                 (0x07 << 2) // configs Virtual Channel ID for DBI traffic
#define DW_DSI_DBI_CFG                  (0x08 << 2) // configs the bit width of pixels for DBI
#define DW_DSI_DBI_PARTITIONING_EN      (0x09 << 2) // host partition DBI traffic automatically
#define DW_DSI_DBI_CMDSIZE              (0x0A << 2) // cmd size for auto partitioning of DBI
#define DW_DSI_PCKHDL_CFG               (0x0B << 2) // how EoTp, BTA, CRC and ECC are to be used
#define DW_DSI_GEN_VCID                 (0x0C << 2) // Virt Channel ID of READ responses to store
#define DW_DSI_MODE_CFG                 (0x0D << 2) // mode of op between Video or Command Mode
#define DW_DSI_VID_MODE_CFG             (0x0E << 2) // Video mode operation config
#define DW_DSI_VID_PKT_SIZE             (0x0F << 2) // video packet size
#define DW_DSI_VID_NUM_CHUNKS           (0x10 << 2) // number of chunks to use
#define DW_DSI_VID_NULL_SIZE            (0x11 << 2) // configs the size of null packets
#define DW_DSI_VID_HSA_TIME             (0x12 << 2) // configs the video HSA time
#define DW_DSI_VID_HBP_TIME             (0x13 << 2) // configs the video HBP time
#define DW_DSI_VID_HLINE_TIME           (0x14 << 2) // configs the overall time for each video line
#define DW_DSI_VID_VSA_LINES            (0x15 << 2) // configs the VSA period
#define DW_DSI_VID_VBP_LINES            (0x16 << 2) // configs the VBP period
#define DW_DSI_VID_VFP_LINES            (0x17 << 2) // configs the VFP period
#define DW_DSI_VID_VACTIVE_LINES        (0x18 << 2) // configs the vertical resolution of video
#define DW_DSI_EDPI_CMD_SIZE            (0x19 << 2) // configs the size of eDPI packets
#define DW_DSI_CMD_MODE_CFG             (0x1A << 2) // command mode operation config
#define DW_DSI_GEN_HDR                  (0x1B << 2) // header for new packets
#define DW_DSI_GEN_PLD_DATA             (0x1C << 2) // payload for packets sent using the Gen i/f
#define DW_DSI_CMD_PKT_STATUS           (0x1D << 2) // info about FIFOs related to DBI and Gen i/f
#define DW_DSI_TO_CNT_CFG               (0x1E << 2) // counters that trig timeout errors
#define DW_DSI_HS_RD_TO_CNT             (0x1F << 2) // Peri Resp timeout after HS Rd operations
#define DW_DSI_LP_RD_TO_CNT             (0x20 << 2) // Peri Resp timeout after LP Rd operations
#define DW_DSI_HS_WR_TO_CNT             (0x21 << 2) // Peri Resp timeout after HS Wr operations
#define DW_DSI_LP_WR_TO_CNT             (0x22 << 2) // Peri Resp timeout after LP Wr operations
#define DW_DSI_BTA_TO_CNT               (0x23 << 2) // Peri Resp timeout after Bus Turnaround comp
#define DW_DSI_SDF_3D                   (0x24 << 2) // 3D cntrl info for VSS packets in video mode.
#define DW_DSI_LPCLK_CTRL               (0x25 << 2) // non continuous clock in the clock lane.
#define DW_DSI_PHY_TMR_LPCLK_CFG        (0x26 << 2) // time for the clock lane
#define DW_DSI_PHY_TMR_CFG              (0x27 << 2) // time for the data lanes
#define DW_DSI_PHY_RSTZ                 (0x28 << 2) // controls resets and the PLL of the D-PHY.
#define DW_DSI_PHY_IF_CFG               (0x29 << 2) // number of active lanes
#define DW_DSI_PHY_ULPS_CTRL            (0x2A << 2) // entering and leaving ULPS in the D- PHY.
#define DW_DSI_PHY_TX_TRIGGERS          (0x2B << 2) // pins that activate triggers in the D-PHY
#define DW_DSI_PHY_STATUS               (0x2C << 2) // contains info about the status of the D- PHY
#define DW_DSI_PHY_TST_CTRL0            (0x2D << 2) // controls clock and clear pins of the D-PHY
#define DW_DSI_PHY_TST_CTRL1            (0x2E << 2) // controls data and enable pins of the D-PHY
#define DW_DSI_INT_ST0                  (0x3F << 2) // status of intr from ack and D-PHY
#define DW_DSI_INT_ST1                  (0x30 << 2) // status of intr related to timeout, ECC, etc
#define DW_DSI_INT_MSK0                 (0x31 << 2) // masks interrupts that affect the INT_ST0 reg
#define DW_DSI_INT_MSK1                 (0x32 << 2) // masks interrupts that affect the INT_ST1 reg

//////////////////////////////////////////////////
// DesignWare MIPI DSI Register Bit Definition
//////////////////////////////////////////////////

// DW_DSI_PWR_UP Register Def
#define PWR_UP_RST                      (0)
#define PWR_UP_ON                       (1)

// DW_DSI_GEN_HDR Register Bit Def
#define GEN_HDR_WC_MSB(x)               ((x & 0xFF) << 16)
#define GEN_HDR_WC_LSB(x)               ((x & 0xFF) << 8)
#define GEN_HDR_VC(x)                   ((x & 0x03) << 6)
#define GEN_HDR_DT(x)                   ((x & 0x3F) << 0)

// DW_DSI_CMD_PKT_STATUS Register Bit Def
#define CMD_PKT_STATUS_RD_CMD_BUSY      (6)
#define CMD_PKT_STATUS_PLD_R_FULL       (5)
#define CMD_PKT_STATUS_PLD_R_EMPTY      (4)
#define CMD_PKT_STATUS_PLD_W_FULL       (3)
#define CMD_PKT_STATUS_PLD_W_EMPTY      (2)
#define CMD_PKT_STATUS_CMD_FULL         (1)
#define CMD_PKT_STATUS_CMD_EMPTY        (0)

// DW_DSI_CLKMGR_CFG Register Bit Def
#define CLKMGR_CFG_TO_CLK_DIV(x)        ((x & 0xFF) << 8)
#define CLKMGR_CFG_TX_ESC_CLK_DIV(x)    ((x & 0xFF) << 0)

// DW_DSI_PCKHDL_CFG Register Bit Def
#define PCKHDL_CFG_CRC_RX_EN            (1 << 4)
#define PCKHDL_CFG_ECC_RX_EN            (1 << 3)
#define PCKHDL_CFG_BTA_EN               (2)
#define PCKHDL_CFG_EN_CRC_ECC           (PCKHDL_CFG_ECC_RX_EN | PCKHDL_CFG_CRC_RX_EN)

// DW_DSI_VID_MODE_CFG Register Bit Def
#define VID_MODE_CFG_LP_EN_ALL          (0x1ff << 8)
#define VID_MODE_CFG_VID_MODE_TYPE(x)   (x << 0)
#define VID_MODE_TYPE_BURST_MODE        (2)


// DW_DSI_PHY_STATUS Register Bit Def
#define PHY_STATUS_PHY_STOPSTATECLKLANE         (2)
#define PHY_STATUS_PHY_DIRECTION                (1)
#define PHY_STATUS_PHY_LOCK                     (0)

#define PHY_TX                                  (0)
#define PHY_RX                                  (1)

// DW_DSI_CMD_MODE_CFG Register Bit Def
#define CMD_MODE_CFG_ACK_RQST_EN                (1)
#define CMD_MODE_CFG_CMD_LP_ALL                 (0x10F7F00)

// DW_DSI_PHY_IF_CFG Register Bit Def
#define PHY_IF_CFG_STOP_WAIT_TIME               (0x28 << 8) // value from vendor
#define PHY_IF_CFG_N_LANES(x)                   ((x - 1) << 0)

// DW_DSI_DPI_LP_CMD_TIM Register Bit Def
#define LP_CMD_TIM_OUTVACT(x)                   (x << 16)
#define LP_CMD_TIM_INVACT(x)                    (x << 0)

// DW_DSI_DPI_COLOR_CODING Register Bit Def
#define DPI_COLOR_CODING(x)                     (x << 0)
#define MIPI_DSI_COLOR_24BIT                    (0x5)

// DW_DSI_PHY_TMR_LPCLK_CFG Register Bit Def
#define PHY_TMR_LPCLK_CFG_CLKHS_TO_LP(x)       (x << 16)
#define PHY_TMR_LPCLK_CFG_CLKLP_TO_HS(x)       (x << 0)

// DW_DSI_PHY_TMR_CFG Register Bit Def
#define PHY_TMR_CFG_HS_TO_LP(x)                 (x << 16)
#define PHY_TMR_CFG_LP_TO_HS(x)                 (x << 0)

// DW_DSI_PHY_RSTZ Register Bit Def
#define PHY_RSTZ_PWR_UP                         (0xf)

// DW_DSI_LPCLK_CTRL Register Bit Def
#define LPCLK_CTRL_AUTOCLKLANE_CTRL             (1)
#define LPCLK_CTRL_TXREQUESTCLKHS               (0)

// Default FIFO Depth based on spec. This value may change based on how the DW IP
// block is synthesized.
#define DWC_DEFAULT_MAX_PLD_FIFO_DEPTH          (200)

// Generic retry value used for BTA and FIFO related events
#define MIPI_DSI_RETRY_MAX                    (3000)

// Assigned Virtual Channel ID for Astro
// TODO: Will need to generate and maintain VCID for multi-display
// solutions

#define MIPI_DSI_VIRTUAL_CHAN_ID                (0)
