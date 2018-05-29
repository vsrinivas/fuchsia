// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// TOP MIPI_DSI AML Registers
#define MIPI_DSI_TOP_SW_RESET                   (0xF0 << 2)
#define MIPI_DSI_TOP_CLK_CNTL                   (0xF1 << 2)
#define MIPI_DSI_TOP_CNTL                       (0xF2 << 2)
#define MIPI_DSI_TOP_SUSPEND_CNTL               (0xF3 << 2)
#define MIPI_DSI_TOP_SUSPEND_LINE               (0xF4 << 2)
#define MIPI_DSI_TOP_SUSPEND_PIX                (0xF5 << 2)
#define MIPI_DSI_TOP_MEAS_CNTL                  (0xF6 << 2)
#define MIPI_DSI_TOP_STAT                       (0xF7 << 2)
#define MIPI_DSI_TOP_MEAS_STAT_TE0              (0xF8 << 2)
#define MIPI_DSI_TOP_MEAS_STAT_TE1              (0xF9 << 2)
#define MIPI_DSI_TOP_MEAS_STAT_VS0              (0xFA << 2)
#define MIPI_DSI_TOP_MEAS_STAT_VS1              (0xFB << 2)
#define MIPI_DSI_TOP_INTR_CNTL_STAT             (0xFC << 2)
#define MIPI_DSI_TOP_MEM_PD                     (0xFD << 2)

#define MIPI_DSI_PHY_CTRL                       (0x000 << 2)
#define MIPI_DSI_CHAN_CTRL                      (0x001 << 2)
#define MIPI_DSI_CHAN_STS                       (0x002 << 2)
#define MIPI_DSI_CLK_TIM                        (0x003 << 2)
#define MIPI_DSI_HS_TIM                         (0x004 << 2)
#define MIPI_DSI_LP_TIM                         (0x005 << 2)
#define MIPI_DSI_ANA_UP_TIM                     (0x006 << 2)
#define MIPI_DSI_INIT_TIM                       (0x007 << 2)
#define MIPI_DSI_WAKEUP_TIM                     (0x008 << 2)
#define MIPI_DSI_LPOK_TIM                       (0x009 << 2)
#define MIPI_DSI_LP_WCHDOG                      (0x00a << 2)
#define MIPI_DSI_ANA_CTRL                       (0x00b << 2)
#define MIPI_DSI_CLK_TIM1                       (0x00c << 2)
#define MIPI_DSI_TURN_WCHDOG                    (0x00d << 2)
#define MIPI_DSI_ULPS_CHECK                     (0x00e << 2)
#define MIPI_DSI_TEST_CTRL0                     (0x00f << 2)
#define MIPI_DSI_TEST_CTRL1                     (0x010 << 2)

// MIPI_DSI_TOP_CNTL Bit definitions
#define TOP_CNTL_DPI_CLR_MODE_START             (20)
#define TOP_CNTL_DPI_CLR_MODE_BITS              (4)
#define TOP_CNTL_IN_CLR_MODE_START              (16)
#define TOP_CNTL_IN_CLR_MODE_BITS               (3)
#define TOP_CNTL_CHROMA_SUBSAMPLE_START         (14)
#define TOP_CNTL_CHROMA_SUBSAMPLE_BITS          (2)

// MIPI_DSI_PHY_CTRL Bit definitions
#define PHY_CTRL_TXDDRCLK_EN                    (1 << 0)
#define PHY_CTRL_DDRCLKPATH_EN                  (1 << 7)
#define PHY_CTRL_CLK_DIV_COUNTER                (1 << 8)
#define PHY_CTRL_CLK_DIV_EN                     (1 << 9)
#define PHY_CTRL_BYTECLK_EN                     (1 << 12)
#define PHY_CTRL_RST_START                      (31)
#define PHY_CTRL_RST_BITS                       (1)

#define ANA_UP_TIME                             (0x100) // from vendor
#define LPOK_TIME                               (0x7C)
#define ULPS_CHECK_TIME                         (0x927C)
#define LP_WCHDOG_TIME                          (0x1000)
#define TURN_WCHDOG_TIME                        (0x1000)

// Frequency Ranges (in KHz) specific to AmLogic S905D2
#define FIN_FREQ_KHZ                            (24 * 1000)
#define MIN_PLL_VCO_KHZ                         (3000 * 1000)
#define MAX_PLL_VCO_KHZ                         (6000 * 1000)
#define MIN_PLL_FREQ_KHZ                        (MIN_PLL_VCO_KHZ / 16)
#define MAX_PLL_FREQ_KHZ                        (MAX_PLL_VCO_KHZ)
#define MAX_PIXEL_CLK_KHZ                       (200 * 1000)
#define MAX_OD_SEL                              (3)
#define PLL_FRAC_RANGE                          (1 << 17)

// We currently only support 8 bit mode
#define SUPPORTED_LCD_BITS                      (8)
#define SUPPORTED_DPI_FORMAT                    (MIPI_DSI_COLOR_24BIT)
#define SUPPORTED_VENC_DATA_WIDTH               (MIPI_DSI_VENC_COLOR_24B)
#define SUPPORTED_VIDEO_MODE_TYPE               (VID_MODE_TYPE_BURST_MODE)

// AML PHY Timer Config Values
#define PHY_TMR_LPCLK_CLKHS_TO_LP               (0x87)
#define PHY_TMR_LPCLK_CLKLP_TO_HS               (0x25)
#define PHY_TMR_HS_TO_LP                        (0x0332)
#define PHY_TMR_LP_TO_HS                        (0x0)
#define DPHY_TIMEOUT                            (200000)

// This defined the number of bytes of the largest packet that can fit in LP mode during
// various regions (VSA, VBP, VFP, VACT).
#define LPCMD_PKT_SIZE                          (4)


#define max(x, y)                               ((x > y)? x : y)

// The following values are based on MIPI D-PHY Spec Version 2.1 Table 14. The
// values defined are recommended values coming from AmLogic

// x100 multiplier to ensure proper ui value
#define UI_X_100                                (100)
// Time that the transmitter continues to send HS clock after the last associated Data Lane
// has transitioned to LP Mode. Interval is defined as the period from the end of HS-Trail
// to the beginning of CLK-TRAIL (>60+52*ui)
#define DPHY_TIME_CLK_POST(ui)                  (2 * (60 * UI_X_100 + 52 * ui))

// Time that the HS clock shall be driven by the transmitter prior to any associated Data Lane
// beginging the transition from LP to HS mode.  (>8*ui)
#define DPHY_TIME_CLK_PRE(ui)                   (10 * ui)

// Time that the transmitter drives the Clock Lane LP-00 Line state immediately
// before the HS-0 Line state starting the HS transmission (38, 95)
#define DPHY_TIME_CLK_PREPARE                   (50 * UI_X_100)

// Time that the transmitter drives the HS-0 state after the last payload clock bit of a
// HS transmission burst  (>60ns)
#define DPHY_TIME_CLK_TRAIL                     (70 * UI_X_100)

// CLK-PREPARE + time the transmitter drives the HS-0 state prior to starting the Clock (> 300)
#define DPHY_TIME_CLK_ZERO(ui)                  (320 * UI_X_100 - DPHY_TIME_CLK_PREPARE)

// Transmitted time internal from the start of HS-TRAIL or CLK-TRAIL to start of
// LP-11 state following a HS burst
#define DPHY_TIME_EOT(ui)                       (105 * UI_X_100 + 12 * ui)

// Time that the transmitter drives the LP-11 following a HS burst (>100ns)
#define DPHY_TIME_HS_EXIT                       (110 * UI_X_100)

// Time that the transmitter drives the Data Lane LP-00 Line state immediately
// before the HS-0 Line state starting the HS transmission (40+4*ui, 85+6*ui)
#define DPHY_TIME_HS_PREPARE(ui)                (50 * UI_X_100 + 4 * ui)

// HS_PREPARE + time that the transmitter drives the HS-0 state prior to transmitting
// the Sync sequence  (>145+10*ui)
#define DPHY_TIME_HS_ZERO(ui)               (160 * UI_X_100  + 10 * ui - DPHY_TIME_HS_PREPARE(ui))

// Time that the transmitter drives the flipped differential state after last
// payload data bit of a HS transmissoin burst  max(n*8*ui, 60+n*4*ui) <n = 1>
#define DPHY_TIME_HS_TRAIL(ui)                  (max((8 * ui), (60 * UI_X_100  + 4 * ui)))

// >100us
#define DPHY_TIME_INIT                          (110 * UI_X_100  * 1000)

// TX length of any LP state should be >50ns according to MIPI D-PHY Spec
#define DPHY_TIME_LP_LPX                        (100 * UI_X_100 )

// Time that the new transmitter drives the Bridge state (LP-00) after
// accepting control during a Link Trunaround (5*lpx)
#define DPHY_TIME_LP_TA_GET                     (5 * DPHY_TIME_LP_LPX)

// Time that the transmitter drives the Bridge state (LP-00) before releasing
// control during a Link Turnaround  (4*lpx)
#define DPHY_TIME_LP_TA_GO                      (4 * DPHY_TIME_LP_LPX)

// Time that the new transmitter waits after the LP-10 state before transmitting
// the Bridge state (LP-00) during a Link Turnaround (lpx, 2*lpx)
#define DPHY_TIME_LP_TA_SURE                    (DPHY_TIME_LP_LPX)

// Time that a transmitter drives a Mark-1 state prior to a Stop state in order to initiate an
// exit from ULPS >1ms
#define DPHY_TIME_WAKEUP                        (1020 * UI_X_100 * 1000)

// LP TX excape mode should be  >100ns --> TODO: Where is this coming from?
#define DPHY_TIME_LP_TESC                       (250 * UI_X_100 )

//  MIPI DSI/VENC Color Format Definitions
#define MIPI_DSI_VENC_COLOR_24B                 (0x1)

// This structure holds the parameters used to program VPU LCD interface
typedef struct {
    uint32_t vid_pixel_on;
    uint32_t vid_line_on;
    uint32_t de_hs_addr;
    uint32_t de_he_addr;
    uint32_t de_vs_addr;
    uint32_t de_ve_addr;
    uint32_t hs_hs_addr;
    uint32_t hs_he_addr;
    uint32_t hs_vs_addr;
    uint32_t hs_ve_addr;
    uint32_t vs_hs_addr;
    uint32_t vs_he_addr;
    uint32_t vs_vs_addr;
    uint32_t vs_ve_addr;
} lcd_timing_t;

// This structure holds the calculated pll values based on desired pixel clock
typedef struct { // unit: kHz
    // IN-OUT parameters
    uint32_t fin;
    uint32_t fout;

    // calculated bitrate
    uint32_t bitrate;

    // pll parameters
    uint32_t pll_m;
    uint32_t pll_n;
    uint32_t pll_fvco;
    uint32_t pll_od1_sel;
    uint32_t pll_od2_sel;
    uint32_t pll_od3_sel;
    uint32_t pll_frac;
    uint32_t pll_fout;
} pll_config_t;
