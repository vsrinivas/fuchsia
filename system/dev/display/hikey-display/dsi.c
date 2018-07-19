// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <hw/reg.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/gpio.h>

#include "dsi.h"
#include "adv7533.h"

static zx_status_t dsi_get_display_timing(dsi_t* dsi) {
    zx_status_t status;
    uint8_t* edid_buf = adv7533_get_edid_buffer();
    uint8_t num_dtd = 0;

    if (edid_buf == 0) {
        zxlogf(ERROR, "%s: No EDID available\n", __FUNCTION__);
        return ZX_ERR_NOT_FOUND;
    }

    dsi->std_raw_dtd = calloc(1, sizeof(detailed_timing_t));
    dsi->std_disp_timing = calloc(1, sizeof(disp_timing_t));
    if (dsi->std_raw_dtd == 0 || dsi->std_disp_timing == 0) {
        return ZX_ERR_NO_MEMORY;
    }

    edid_parse_std_display_timing(edid_buf, dsi->std_raw_dtd, dsi->std_disp_timing);

    if ( (status = edid_get_num_dtd(edid_buf, &num_dtd)) != ZX_OK) {
        zxlogf(ERROR, "Something went wrong with reading number of DTD\n");
        return status;
    }

    if (num_dtd == 0) {
        zxlogf(ERROR, "No DTD Founds!!\n");
        return ZX_ERR_INTERNAL;
    }

    zxlogf(INFO, "Number of DTD found was %d\n", num_dtd);
    dsi->raw_dtd = calloc(num_dtd, sizeof(detailed_timing_t));
    dsi->disp_timing = calloc(num_dtd, sizeof(disp_timing_t));
    if (dsi->raw_dtd == 0 || dsi->disp_timing == 0) {
        return ZX_ERR_NO_MEMORY;
    }

    edid_parse_display_timing(edid_buf, dsi->raw_dtd, dsi->disp_timing, num_dtd);

    return ZX_OK;
}

/* Unknown DPHY.  Use hardcoded values from Android source code for now */
static void dsi_dphy_write(dsi_t* dsi, uint32_t reg, uint32_t val)
{

    // Select phy register
    DW_DSI_WRITE32(DW_DSI_PHY_TST_CTRL1, (reg | DW_DSI_PHY_TST_CTRL1_TESTEN));
    // pulse
    DW_DSI_WRITE32(DW_DSI_PHY_TST_CTRL0, DW_DSI_PHY_TST_CTRL0_TSTCLK);
    DW_DSI_WRITE32(DW_DSI_PHY_TST_CTRL0, DW_DSI_PHY_TST_CTRL0_TSTCLR);

    // write value (for the register selected above)
    DW_DSI_WRITE32(DW_DSI_PHY_TST_CTRL1, val);

    // pulse
    DW_DSI_WRITE32(DW_DSI_PHY_TST_CTRL0, DW_DSI_PHY_TST_CTRL0_TSTCLK);
    DW_DSI_WRITE32(DW_DSI_PHY_TST_CTRL0, DW_DSI_PHY_TST_CTRL0_TSTCLR);
}

static void dsi_configure_dphy_pll(dsi_t* dsi) {
    uint32_t i;
    uint32_t tmp = 0;

    dsi_dphy_write(dsi, 0x14, (0x1 << 4) + (0x0 << 3) + (0x0 << 2) + 0x0);
    dsi_dphy_write(dsi, 0x15, 0x2d);
    dsi_dphy_write(dsi, 0x16, (0x1 << 5) + (0x0 << 4) +0x1);
    dsi_dphy_write(dsi, 0x17, 0x2);
    dsi_dphy_write(dsi, 0x1D, 0x55);
    dsi_dphy_write(dsi, 0x1E, (0x3 << 5) + (0x1 << 4) + (0x1 << 3) + (0x0 << 2) + (0x0 << 1) + 0x1);
    dsi_dphy_write(dsi, 0x1F, 0x5a);
    dsi_dphy_write(dsi, 0x20, (0x0));
    dsi_dphy_write(dsi, 0x21, 0x28);
    dsi_dphy_write(dsi, 0x22, 0xc);
    dsi_dphy_write(dsi, 0x23, 0x9);
    dsi_dphy_write(dsi, 0x24, 0x1a);
    dsi_dphy_write(dsi, 0x25, 0xa);

    for (i = 0; i < DS_NUM_LANES; i++) {
        tmp = 0x30 + (i << 4);
        dsi_dphy_write(dsi, tmp, 0x3c);
        tmp = 0x31 + (i << 4);
        dsi_dphy_write(dsi, tmp, 0x0);
        dsi_dphy_write(dsi, tmp, 0xc);
        tmp = 0x33 + (i << 4);
        dsi_dphy_write(dsi, tmp, 0x8);
        tmp = 0x34 + (i << 4);
        dsi_dphy_write(dsi, tmp, 0xb);
        tmp = 0x35 + (i << 4);
        dsi_dphy_write(dsi, tmp, 0xb);
        tmp = 0x36 + (i << 4);
        dsi_dphy_write(dsi, tmp, 0x3);
        tmp = 0x37 + (i << 4);
        dsi_dphy_write(dsi, tmp, 0x4);
    }
}

static void hdmi_gpio_init(dsi_t* dsi) {
    gpio_protocol_t* gpio = &dsi->hdmi_gpio.gpio;
    gpio_config(gpio, GPIO_MUX, GPIO_DIR_OUT);
    gpio_config(gpio, GPIO_PD, GPIO_DIR_OUT);
    gpio_config(gpio, GPIO_INT, GPIO_DIR_IN);
    gpio_write(gpio, GPIO_MUX, 0);
}

static void dsi_release(void* ctx) {
    dsi_t* dsi = ctx;
    free(dsi);
}

static zx_status_t dsi_get_protocol(void* ctx, uint32_t proto_id, void* out) {
        return ZX_OK;
}

static zx_protocol_device_t dsi_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = dsi_get_protocol,
    .release = dsi_release,
};

static zx_status_t dsi_mipi_test(dsi_t* dsi) {

    // enable video mode
    DW_DSI_SET_BITS32(DW_DSI_MODE_CFG, 0x0, 1, 0);

    // configure dpi color coding
    DW_DSI_SET_BITS32(DW_DSI_DPI_COLOR_CODING, 0x5, 4, 0);

    DW_DSI_SET_BITS32(DW_DSI_VID_MODE_CFG, 1, 1, 16);

    return ZX_OK;
}

static zx_status_t dsi_configure_dphy(dsi_t* dsi) {
    uint32_t tmp = 0;

    // D-PHY shutdown and reset
    DW_DSI_WRITE32(DW_DSI_PHY_RSTZ, DW_DSI_PHY_RSTZ_SHUTDOWN);

    // Configure number of lanes
    DW_DSI_SET_BITS32(DW_DSI_PHY_IF_CFG, (DS_NUM_LANES - 1), 2, 0);

    // Configure TX_EST to frequency lower than 20MHz. Since byte clock is limited to 187.5MHz,
    // write 0x09 will always generate clock less than 20MHz
    DW_DSI_SET_BITS32(DW_DSI_CLKMGR_CFG, 0x09, 8, 0);

    // Configure PHY PLL values
    dsi_configure_dphy_pll(dsi);

    // Enable PHY
    DW_DSI_WRITE32(DW_DSI_PHY_RSTZ, DW_DSI_PHY_RSTZ_ENABLE);

    // Wait for it to compelete
    // TODO: Define some sort of timeout
    tmp = DW_DSI_READ32(DW_DSI_PHY_STATUS);
    while ((tmp & DW_DSI_PHY_STATUS_PHY_LOCKED) != DW_DSI_PHY_STATUS_PHY_LOCKED) {
        usleep(1000);
        tmp = DW_DSI_READ32(DW_DSI_PHY_STATUS);
    }

    /* Wait for all four lan*/
    // TODO: Define some sort of timeout
    tmp = DW_DSI_READ32(DW_DSI_PHY_STATUS);
    while ((tmp & DW_DSI_PHY_STATUS_ALLSTOP) != DW_DSI_PHY_STATUS_ALLSTOP) {
        usleep(1000);
        tmp = DW_DSI_READ32(DW_DSI_PHY_STATUS);
    }

    return ZX_OK;
}

static void dsi_configure_dpi_interface(dsi_t* dsi) {
    // Configure the virtual channel of the generated packets (0 since single display mode)
    DW_DSI_SET_BITS32(DW_DSI_DPI_VCID, 0x0, 2, 0);

    // Configure bpp (bits per pixel). Set to 24 bit
    DW_DSI_SET_BITS32(DW_DSI_DPI_COLOR_CODING, DSI_COLOR_CODE_24BITS, 4, 0);

    // Configure the polarity of dpidataen (active high)
    DW_DSI_SET_BITS32(DW_DSI_DPI_CFG_POL, DSI_CFG_POL_ACTIVE_HIGH, 1,
        DW_DSI_DPI_CFG_POL_DATAEN_START);

    // Configure the polarity of vsync (active high)
    DW_DSI_SET_BITS32(DW_DSI_DPI_CFG_POL, DSI_CFG_POL_ACTIVE_HIGH, 1,
        DW_DSI_DPI_CFG_POL_VSYNC_START);

    // Configure the polarity of hsync (active high)
    DW_DSI_SET_BITS32(DW_DSI_DPI_CFG_POL, DSI_CFG_POL_ACTIVE_HIGH, 1,
        DW_DSI_DPI_CFG_POL_HSYNC_START);

    // Configure the polarity of shutd (active high)
    DW_DSI_SET_BITS32(DW_DSI_DPI_CFG_POL, DSI_CFG_POL_ACTIVE_HIGH, 1,
        DW_DSI_DPI_CFG_POL_SHUTD_START);

    // Configure the polarity of colorm (active high)
    DW_DSI_SET_BITS32(DW_DSI_DPI_CFG_POL, DSI_CFG_POL_ACTIVE_HIGH, 1,
        DW_DSI_DPI_CFG_POL_COLORM_START);

}

static zx_status_t dsi_mipi_init(dsi_t* dsi) {
    uint64_t pixel_clk = 0;
    uint32_t hdisplay;
    uint32_t vdisplay;
    uint32_t hsync_start;
    uint32_t hsync_end;
    uint32_t htotal;
    uint32_t vsync_start;
    uint32_t vsync_end;
    uint32_t vtotal;
    uint32_t hfp;
    uint32_t hbp;
    uint32_t vfp;
    uint32_t vbp;
    uint32_t hpw;
    uint32_t vpw;
    uint32_t hsa_time;
    uint32_t hbp_time;
    uint32_t hline_time;

    /* Below values are calculatd based on PHY parameters which we don't know */
    uint32_t clk_lane_lp2hs_time = 0x3f;
    uint32_t clk_lane_hs2lp_time = 0x3a;
    uint32_t data_lane_lp2hs_time = 0x68;
    uint32_t data_lane_hs2lp_time = 0x13;

    // reset core
    DW_DSI_WRITE32(DW_DSI_PWR_UP, 0);

    // Configure DPHY
    dsi_configure_dphy(dsi);

    /* MIPI-DSI Spec Section 3.1.1 */
    dsi_configure_dpi_interface(dsi);

    // Configure low-power transitions whenever possible
    DW_DSI_SET_BITS32(DW_DSI_VID_MODE_CFG, DW_DSI_VID_MODE_CFG_ALL_LP,
        DW_DSI_VID_MODE_CFG_LP_ALL_BITS, DW_DSI_VID_MODE_CFG_LP_ALL_START);

    // Configure whether controller should request ack msg at end of frame (no need)
    DW_DSI_SET_BITS32(DW_DSI_VID_MODE_CFG, 0x0, DW_DSI_VID_MODE_CFG_FRAME_ACK_BITS,
        DW_DSI_VID_MODE_CFG_FRAME_ACK_START);

    // Configure commands to be sent in low power mode only
    DW_DSI_SET_BITS32(DW_DSI_VID_MODE_CFG, 0x1, DW_DSI_VID_MODE_CFG_LP_CMD_BITS,
        DW_DSI_VID_MODE_CFG_LP_CMD_START);

    // set mode to Non-burst with sync pulses
    DW_DSI_SET_BITS32(DW_DSI_VID_MODE_CFG, DSI_NON_BURST_SYNC_PULSES,
        DW_DSI_VID_MODE_CFG_MODE_BITS, DW_DSI_VID_MODE_CFG_MODE_START);

    // Set number of pixel in a single video packet
    DW_DSI_SET_BITS32(DW_DSI_VID_PKT_SIZE, dsi->std_disp_timing->HActive,
        DW_DSI_VID_PKT_SIZE_BITS, DW_DSI_VID_PKT_SIZE_START);

    // Configure number of packets to be transmitted per video line (0 for single transmission)
    DW_DSI_WRITE32(DW_DSI_VID_NUM_CHUNKS, 0);

    // Disable null packet
    DW_DSI_WRITE32(DW_DSI_VID_NULL_SIZE, 0);

    /* TODO: fix blank display bug when set backlight*/
    DW_DSI_SET_BITS32(DW_DSI_DPI_LP_CMD_TIM, 0x4, 8, 16);

    /* for dsi read, BTA enable*/
    DW_DSI_SET_BITS32(DW_DSI_PCKHDL_CFG, 0x1, 1, 2);

    // Define DPI Horizontal and Vertical timing configuration


    // TODO: This is a hardcoded value in the Android source.
    // Supposed to be: pixel_clk = dsi->std_disp_timing->pixel_clk * 10000;
    pixel_clk = 144000000;
    hdisplay = dsi->std_disp_timing->HActive;
    vdisplay = dsi->std_disp_timing->VActive;
    hsync_start = dsi->std_disp_timing->HActive + dsi->std_disp_timing->HSyncOffset;
    vsync_start = dsi->std_disp_timing->VActive + dsi->std_disp_timing->VSyncOffset;
    hsync_end = hsync_start + dsi->std_disp_timing->HSyncPulseWidth;
    vsync_end = vsync_start + dsi->std_disp_timing->VSyncPulseWidth;
    htotal = dsi->std_disp_timing->HActive + dsi->std_disp_timing->HBlanking;
    vtotal = dsi->std_disp_timing->VActive + dsi->std_disp_timing->VBlanking;

    hfp = hsync_start - hdisplay;
    hbp = htotal - hsync_end;
    hpw = hsync_end - hsync_start;
    vfp = vsync_start - vdisplay;
    vbp = vtotal - vsync_end;
    vpw = vsync_end - vsync_start;

    hsa_time = (hpw * LANE_BYTE_CLOCK) / pixel_clk;
    hbp_time = (hbp * LANE_BYTE_CLOCK) / pixel_clk;
    hline_time = ROUND1((hpw + hbp + hfp + hdisplay) * LANE_BYTE_CLOCK, pixel_clk);

    DW_DSI_SET_BITS32(DW_DSI_VID_HSA_TIME, hsa_time, 12, 0);
    DW_DSI_SET_BITS32(DW_DSI_VID_HBP_TIME, hbp_time, 12, 0);
    DW_DSI_SET_BITS32(DW_DSI_VID_HLINE_TIME, hline_time, 15, 0);

    /* Define the Vertical line configuration*/
    DW_DSI_SET_BITS32(DW_DSI_VID_VSA_LINES, vpw, 10, 0);
    DW_DSI_SET_BITS32(DW_DSI_VID_VBP_LINES, vbp, 10, 0);
    DW_DSI_SET_BITS32(DW_DSI_VID_VFP_LINES, vfp, 10, 0);
    DW_DSI_SET_BITS32(DW_DSI_VID_VACTIVE_LINES, vdisplay, 14, 0);
    DW_DSI_SET_BITS32(DW_DSI_TO_CNT_CFG, 0x7FF, 16, 0);

    /* Configure core's phy parameters*/
    DW_DSI_SET_BITS32(DW_DSI_PHY_TMR_LPCLK_CFG, clk_lane_lp2hs_time, 10, 0);
    DW_DSI_SET_BITS32(DW_DSI_PHY_TMR_LPCLK_CFG, clk_lane_hs2lp_time, 10, 16);

    DW_DSI_SET_BITS32(DW_DSI_PHY_TMR_RD_CFG, 0x7FFF, 15, 0);
    DW_DSI_SET_BITS32(DW_DSI_PHY_TMR_CFG, data_lane_lp2hs_time, 10, 0);
    DW_DSI_SET_BITS32(DW_DSI_PHY_TMR_CFG, data_lane_hs2lp_time, 10, 16);

    /* Waking up Core*/
    DW_DSI_SET_BITS32(DW_DSI_PWR_UP, 0x1, 1, 0);

    /* Make sure we are in video mode  */
    DW_DSI_SET_BITS32(DW_DSI_MODE_CFG, 0x0, 1, 0);

    /* Enable EoTp Transmission */
    DW_DSI_SET_BITS32(DW_DSI_PCKHDL_CFG, 0x1, 1, 0);

    /* Generate High Speed clock, Continuous clock */
    DW_DSI_SET_BITS32(DW_DSI_LPCLK_CTRL, 0x1, 2, 0);

    return ZX_OK;
}

static zx_status_t dsi_bind(void* ctx, zx_device_t* parent) {
    zxlogf(INFO, "dsi_bind\n");

    dsi_t* dsi = calloc(1, sizeof(dsi_t));
    if (!dsi) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &dsi->pdev);
    if (status != ZX_OK) {
        goto fail;
    }
    dsi->parent = parent;

    status = pdev_map_mmio_buffer(&dsi->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &dsi->mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dsi_bind: pdev_map_mmio_buffer failed\n");
        goto fail;
    }

    /* Obtain the I2C devices */
    if (device_get_protocol(parent, ZX_PROTOCOL_I2C, &dsi->i2c_dev.i2c) != ZX_OK) {
        zxlogf(ERROR, "%s: Could not obtain I2C Protocol\n", __FUNCTION__);
        goto fail;
    }

    /* Obtain the GPIO devices */
    if (device_get_protocol(parent, ZX_PROTOCOL_GPIO, &dsi->hdmi_gpio.gpio) != ZX_OK) {
        zxlogf(ERROR, "%s: Could not obtain GPIO Protocol\n", __FUNCTION__);
        goto fail;
    }

    hdmi_gpio_init(dsi);

    if ( (status = adv7533_init(dsi)) != ZX_OK) {
        zxlogf(ERROR, "%s: Error in ADV7533 Initialization %d\n", __FUNCTION__, status);
        goto fail;
    }
    dsi_get_display_timing(dsi);
    dsi_mipi_init(dsi);
    hdmi_init(dsi);
    dsi_mipi_test(dsi);

    // dsi_mipi_init(dsi);
    zxlogf(INFO, "MIPI Initialized. Version is 0x%x\n", readl(io_buffer_virt(&dsi->mmio)));

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "dsi",
        .ctx = dsi,
        .ops = &dsi_device_proto,
        .proto_id = 0,
        .proto_ops = 0,
    };

    status = device_add(parent, &args, &dsi->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    return ZX_OK;

fail:
    zxlogf(ERROR, "dsi3_bind failed %d\n", status);
    dsi_release(dsi);
    return status;

}

static zx_driver_ops_t dsi_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = dsi_bind,
};

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(dsi, dsi_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_DSI),
ZIRCON_DRIVER_END(dsi)
// clang-format on
