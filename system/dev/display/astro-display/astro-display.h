// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/canvas.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <threads.h>
#include <hw/reg.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/display-controller.h>
#include <zircon/listnode.h>
#include <zircon/types.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include "hhi.h"
#include "mipi-dsi.h"
#include "dw-mipi-dsi.h"
#include "aml-dsi.h"

#define DISP_ERROR(fmt, ...)    zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...)     zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_SPEW(fmt, ...)     zxlogf(SPEW, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE              zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

#define DISPLAY_MASK(start, count) (((1 << (count)) - 1) << (start))
#define DISPLAY_SET_MASK(mask, start, count, value) \
                        ((mask & ~DISPLAY_MASK(start, count)) | \
                                (((value) << (start)) & DISPLAY_MASK(start, count)))

#define READ32_DMC_REG(a)                   readl(io_buffer_virt(&display->mmio_dmc) + a)
#define WRITE32_DMC_REG(a, v)               writel(v, io_buffer_virt(&display->mmio_dmc) + a)

#define READ32_MIPI_DSI_REG(a)              readl(io_buffer_virt(&display->mmio_mipi_dsi) + a)
#define WRITE32_MIPI_DSI_REG(a, v)          writel(v, io_buffer_virt(&display->mmio_mipi_dsi) + a)

#define READ32_DSI_PHY_REG(a)               readl(io_buffer_virt(&display->mmio_dsi_phy) + a)
#define WRITE32_DSI_PHY_REG(a, v)           writel(v, io_buffer_virt(&display->mmio_dsi_phy) + a)

#define READ32_HHI_REG(a)                   readl(io_buffer_virt(&display->mmio_hhi) + a)
#define WRITE32_HHI_REG(a, v)               writel(v, io_buffer_virt(&display->mmio_hhi) + a);

#define READ32_VPU_REG(a)                   readl(io_buffer_virt(&display->mmio_vpu) + a)
#define WRITE32_VPU_REG(a, v)               writel(v, io_buffer_virt(&display->mmio_vpu) + a)

#define SET_BIT32(x, dest, value, start, count) \
            WRITE32_##x##_REG(dest, (READ32_##x##_REG(dest) & ~DISPLAY_MASK(start, count)) | \
                                (((value) << (start)) & DISPLAY_MASK(start, count)))

#define GET_BIT32(x, dest, start, count) \
            ((READ32_##x##_REG(dest) >> (start)) & ((1 << (count)) - 1))

#define WRITE32_REG(x, a, v)            WRITE32_##x##_REG(a, v)
#define READ32_REG(x, a)                READ32_##x##_REG(a)

#define PANEL_DISPLAY_ID                (1)

// Should match display_mmios table in board driver
enum {
    MMIO_CANVAS,
    MMIO_MPI_DSI,
    MMIO_DSI_PHY,
    MMIO_HHI,
    MMIO_VPU,
};

// Should match display_gpios table in board driver
enum {
    GPIO_BL,
    GPIO_LCD,
    GPIO_PANEL_DETECT,
    GPIO_HW_ID0,
    GPIO_HW_ID1,
    GPIO_HW_ID2,
};

// Astro Display dimension
#define DISPLAY_WIDTH           608
#define DISPLAY_HEIGHT          1024

// Supported panel types
#define PANEL_TV070WSM_FT       0x00
#define PANEL_P070ACB_FT        0x01
#define PANEL_UNKNOWN           0xff

// This display driver supports EVT hardware and onwards. For pre-EVT boards,
// it will simply configure the framebuffer and canvas and assume U-Boot has
// already done all display initializations
#define BOARD_REV_P1            0
#define BOARD_REV_P2            1
#define BOARD_REV_EVT_1         2
#define BOARD_REV_EVT_2         3
#define BOARD_REV_UNKNOWN       0xff

// This structure is populated based on hardware/lcd type. Its values come from vendor.
// This table is the top level structure used to populated all Clocks/LCD/DSI/BackLight/etc
// values
typedef struct {
    uint32_t lane_num;
    uint32_t bit_rate_max;
    uint32_t clock_factor;
    uint32_t lcd_clock;
    uint32_t h_active;
    uint32_t v_active;
    uint32_t h_period;
    uint32_t v_period;
    uint32_t hsync_width;
    uint32_t hsync_bp;
    uint32_t hsync_pol;
    uint32_t vsync_width;
    uint32_t vsync_bp;
    uint32_t vsync_pol;
} display_setting_t;

typedef struct {
    zx_device_t*                        zxdev;
    platform_device_protocol_t          pdev;
    zx_device_t*                        parent;
    zx_device_t*                        mydevice;
    zx_device_t*                        fbdevice;
    zx_handle_t                         bti;
    zx_handle_t                         inth;

    gpio_protocol_t                     gpio;
    i2c_protocol_t                      i2c;
    canvas_protocol_t                   canvas;

    thrd_t                              main_thread;
    thrd_t                              vsync_thread;

    // Lock for general display state, in particular display_id.
    mtx_t                               display_lock;
    // Lock for imported images.
    mtx_t                               image_lock;
    // Lock for the display callback, for enforcing an ordering on
    // hotplug callbacks. Should be acquired before display_lock.
    mtx_t                               cb_lock;
    // TODO(stevensd): This can race if this is changed right after
    // vsync but before the interrupt is handled.
    uint8_t                             current_image;
    bool                                current_image_valid;

    io_buffer_t                         mmio_dmc;
    io_buffer_t                         mmio_mipi_dsi;
    io_buffer_t                         mmio_dsi_phy;
    io_buffer_t                         mmio_hhi;
    io_buffer_t                         mmio_vpu;
    io_buffer_t                         fbuffer;
    zx_handle_t                         vsync_interrupt;

    uint32_t                            width;
    uint32_t                            height;
    uint32_t                            stride;
    zx_pixel_format_t                   format;

    // This flag is used to skip all driver initializations for older
    // boards that we don't support. Those boards will depend on U-Boot
    // to set things up
    bool                                skip_disp_init;

    uint8_t                             board_rev;
    uint8_t                             panel_type;

    lcd_timing_t                        lcd_timing;
    dsi_phy_config_t                    dsi_phy_cfg;
    display_setting_t                   disp_setting;
    pll_config_t                        pll_cfg;

    uint8_t                             input_color_format;
    uint8_t                             output_color_format;
    uint8_t                             color_depth;

    display_controller_cb_t*            dc_cb;
    void*                               dc_cb_ctx;
    list_node_t                         imported_images;

} astro_display_t;

// Below two functions setup the OSD layer
// TODO: The function depends heavily on U-Boot setting up the OSD layer. Write a
// proper OSD driver (ZX-2453)(ZX-2454)
void disable_osd(astro_display_t* display);
zx_status_t configure_osd(astro_display_t* display);
void flip_osd(astro_display_t* display, uint8_t idx);

// Backlight Initialization
void init_backlight(astro_display_t* display);

// Initialize all display related clocks
zx_status_t display_clock_init(astro_display_t* display);

// Useful functions for dumping all display related structures for debug purposes
void dump_display_info(astro_display_t* display);
void dump_dsi_hose(astro_display_t* display);
void dump_dsi_phy(astro_display_t* display);

// This function turns on DSI Host for AmLogic platform
zx_status_t aml_dsi_host_on(astro_display_t* display);

// This function initializes LCD panel
zx_status_t lcd_init(astro_display_t* display);


// This send a generic DSI command
zx_status_t mipi_dsi_cmd(astro_display_t* display, uint8_t* tbuf, size_t tlen,
                                                    uint8_t* rbuf, size_t rlen,
                                                    bool is_dcs);




