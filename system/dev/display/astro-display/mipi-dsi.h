// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define MIPI_DSI_DT_VSYNC_START                 (0x01)
#define MIPI_DSI_DT_VSYNC_END                   (0x11)
#define MIPI_DSI_DT_HSYNC_START                 (0x21)
#define MIPI_DSI_DT_HSYNC_END                   (0x31)
#define MIPI_DSI_DT_EOTP                        (0x08)
#define MIPI_DSI_DT_COLOR_MODE_OFF              (0x02)
#define MIPI_DSI_DT_COLOR_MODE_ON               (0x12)
#define MIPI_DSI_DT_PERI_CMD_OFF                (0x22)
#define MIPI_DSI_DT_PERI_CMD_ON                 (0x32)
#define MIPI_DSI_DT_GEN_SHORT_WRITE_0           (0x03)
#define MIPI_DSI_DT_GEN_SHORT_WRITE_1           (0x13)
#define MIPI_DSI_DT_GEN_SHORT_WRITE_2           (0x23)
#define MIPI_DSI_DT_GEN_SHORT_READ_0            (0x04)
#define MIPI_DSI_DT_GEN_SHORT_READ_1            (0x14)
#define MIPI_DSI_DT_GEN_SHORT_READ_2            (0x24)
#define MIPI_DSI_DT_DCS_SHORT_WRITE_0           (0x05)
#define MIPI_DSI_DT_DCS_SHORT_WRITE_1           (0x15)
#define MIPI_DSI_DT_DCS_READ_0                  (0x06)
#define MIPI_DSI_DT_SET_MAX_RET_PKT             (0x37)
#define MIPI_DSI_DT_NULL_PKT                    (0x09)
#define MIPI_DSI_DT_BLAKING_PKT                 (0x19)
#define MIPI_DSI_DT_GEN_LONG_WRITE              (0x29)
#define MIPI_DSI_DT_DCS_LONG_WRITE              (0x39)
#define MIPI_DSI_DT_YCbCr_422_20BIT             (0x0C)
#define MIPI_DSI_DT_YCbCr_422_24BIT             (0x1C)
#define MIPI_DSI_DT_YCbCr_422_16BIT             (0x2C)
#define MIPI_DSI_DT_RGB_101010                  (0x0D)
#define MIPI_DSI_DT_RGB_121212                  (0x1D)
#define MIPI_DSI_DT_YCbCr_420_12BIT             (0x3D)
#define MIPI_DSI_DT_RGB_565                     (0x0E)
#define MIPI_DSI_DT_RGB_666                     (0x1E)
#define MIPI_DSI_DT_RGB_666_L                   (0x2E)
#define MIPI_DSI_DT_RGB_888                     (0x3E)
#define MIPI_DSI_DT_UNKNOWN                     (0xFF)

#define MIPI_DSI_NO_ACK                         (0)
#define MIPI_DSI_ACK                            (1)

#define VIDEO_MODE                              (0)
#define COMMAND_MODE                            (1)

#define COMMAND_GEN                             (0)
#define COMMAND_DCS                             (1)

/* This is the generic MIPI-DSI comomand structure that
 * can be used as a IP-independent driver
 */
typedef struct {
    uint8_t     virt_chn_id;
    uint8_t     dsi_data_type;

    // TX Direction
    size_t      pld_size;
    uint8_t*    pld_data;

    // RX Direction
    size_t      rsp_size;
    uint8_t*    rsp_data;

    uint32_t    flags;

} mipi_dsi_cmd_t;

/* mipi_dsi_cmd_t flag bit def */
#define MIPI_DSI_CMD_FLAGS_ACK                  (1 << 0)
#define MIPI_DSI_CMD_FLAGS_SET_MAX              (1 << 1)


/* This structure holds the timing parameters used for MIPI D-PHY
 * This can be moved later on to MIPI D-PHY specific header if need be
 */
typedef struct {
    uint32_t lp_tesc;
    uint32_t lp_lpx;
    uint32_t lp_ta_sure;
    uint32_t lp_ta_go;
    uint32_t lp_ta_get;
    uint32_t hs_exit;
    uint32_t hs_trail;
    uint32_t hs_zero;
    uint32_t hs_prepare;
    uint32_t clk_trail;
    uint32_t clk_post;
    uint32_t clk_zero;
    uint32_t clk_prepare;
    uint32_t clk_pre;
    uint32_t init;
    uint32_t wakeup;
} dsi_phy_config_t;
