// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <reg.h>
#include <stdio.h>
#include <trace.h>
#include <string.h>
#include <lib/cbuf.h>
#include <arch/arm64/periphmap.h>
#include <kernel/thread.h>
#include <dev/interrupt.h>
#include <pdev/driver.h>
#include <dev/psci.h>
#include <zircon/boot/driver-config.h>

static vaddr_t preset_base;
static vaddr_t hiu_base;
static vaddr_t hdmitx_base;

#define TOP_OFFSET_MASK      (0x0UL << 24)
#define DWC_OFFSET_MASK      (0x10UL << 24)
#define HDMITX_ADDR_PORT                                (0x00)
#define HDMITX_DATA_PORT                                (0x04)

#define DISPLAY_MASK(start, count) (((1 << (count)) - 1) << (start))
#define DISPLAY_SET_MASK(mask, start, count, value) \
                        ((mask & ~DISPLAY_MASK(start, count)) | \
                                (((value) << (start)) & DISPLAY_MASK(start, count)))

#define READ32_PRESET_REG(a)             readl(preset_base + a)
#define WRITE32_PRESET_REG(a, v)         writel(v, preset_base + a)

#define READ32_HDMITX_REG(a)             readl(hdmitx_base + a)
#define WRITE32_HDMITX_REG(a, v)         writel(v, hdmitx_base + a)

#define READ32_HHI_REG(a)                readl(hiu_base + a)
#define WRITE32_HHI_REG(a, v)            writel(v, hiu_base + a)

#define SET_BIT32(x, dest, value, count, start) \
            WRITE32_##x##_REG(dest, (READ32_##x##_REG(dest) & ~DISPLAY_MASK(start, count)) | \
                                (((value) << (start)) & DISPLAY_MASK(start, count)))

#define HHI_HDMI_CLK_CNTL                               (0x73 << 2)
#define HHI_GCLK_MPEG2                                  (0x52 << 2)
#define HHI_MEM_PD_REG0                                 (0x40 << 2)
#define PRESET0_REGISTER                                (0x404)
#define PRESET2_REGISTER                                (0x40C)
#define HDMITX_TOP_SW_RESET                             (TOP_OFFSET_MASK + 0x000)
#define HDMITX_TOP_CLK_CNTL                             (TOP_OFFSET_MASK + 0x001)
#define HDMITX_DWC_MC_LOCKONCLOCK                       (DWC_OFFSET_MASK + 0x4006)
#define HDMITX_DWC_MC_CLKDIS                            (DWC_OFFSET_MASK + 0x4001)
#define HDMITX_DWC_A_APIINTMSK                          (DWC_OFFSET_MASK + 0x5008)
#define HDMITX_DWC_A_VIDPOLCFG                          (DWC_OFFSET_MASK + 0x5009)
#define HDMITX_DWC_A_OESSWCFG                           (DWC_OFFSET_MASK + 0x500A)

static void hdmitx_writereg(uint32_t addr, uint32_t data) {
    // determine if we are writing to HDMI TOP (AMLOGIC Wrapper) or HDMI IP
    uint32_t offset = (addr & DWC_OFFSET_MASK) >> 24;
    addr = addr & 0xffff;
    WRITE32_HDMITX_REG(HDMITX_ADDR_PORT + offset, addr);
    WRITE32_HDMITX_REG(HDMITX_ADDR_PORT + offset, addr); // FIXME: Need to write twice!
    WRITE32_HDMITX_REG(HDMITX_DATA_PORT + offset, data);
}

static void s912_hdcp_init(const void* driver_data, uint32_t length) {
    ASSERT(length >= sizeof(dcfg_amlogic_hdcp_driver_t));
    const dcfg_amlogic_hdcp_driver_t* driver = driver_data;
    ASSERT(driver->preset_phys && driver->hiu_phys && driver->hdmitx_phys);

    // get virtual addresses of our peripheral bases
    preset_base = periph_paddr_to_vaddr(driver->preset_phys);
    hiu_base = periph_paddr_to_vaddr(driver->hiu_phys);
    hdmitx_base = periph_paddr_to_vaddr(driver->hdmitx_phys);
    ASSERT(preset_base && hiu_base && hdmitx_base);

    // enable clocks
    SET_BIT32(HHI, HHI_HDMI_CLK_CNTL, 0x0100, 16, 0);

    // enable clk81 (needed for HDMI module and a bunch of other modules)
    SET_BIT32(HHI, HHI_GCLK_MPEG2, 1, 1, 4);

    // power up HDMI Memory (bits 15:8)
    SET_BIT32(HHI, HHI_MEM_PD_REG0, 0, 8, 8);

    // reset hdmi related blocks (HIU, HDMI SYS, HDMI_TX)
    WRITE32_PRESET_REG(PRESET0_REGISTER, (1 << 19));
    WRITE32_PRESET_REG(PRESET2_REGISTER, (1 << 15));
    WRITE32_PRESET_REG(PRESET2_REGISTER, (1 << 2));

    // // Bring HDMI out of reset
    hdmitx_writereg(HDMITX_TOP_SW_RESET, 0);
    spin(200);
    hdmitx_writereg(HDMITX_TOP_CLK_CNTL, 0x000000ff);
    hdmitx_writereg(HDMITX_DWC_MC_LOCKONCLOCK, 0xff);
    hdmitx_writereg(HDMITX_DWC_MC_CLKDIS, 0x00);

    /* Configure HDCP */
    uint32_t data32  = 0;
    data32 |= (0 << 7);
    data32 |= (0 << 6);
    data32 |= (0 << 4);
    data32 |= (0 << 3);
    data32 |= (0 << 2);
    data32 |= (0 << 1);
    data32 |= (1 << 0);
    hdmitx_writereg(HDMITX_DWC_A_APIINTMSK, data32);

    data32  = 0;
    data32 |= (0 << 5);
    data32 |= (1 << 4);
    data32 |= (1 << 3);
    data32 |= (1 << 1);
    hdmitx_writereg(HDMITX_DWC_A_VIDPOLCFG, data32);

    hdmitx_writereg(HDMITX_DWC_A_OESSWCFG, 0x40);

    psci_smc_call(0x82000012, 0, 0, 0);
}

LK_PDEV_INIT(s912_hdcp_init, KDRV_AMLOGIC_HDCP, s912_hdcp_init, LK_INIT_LEVEL_PLATFORM);
