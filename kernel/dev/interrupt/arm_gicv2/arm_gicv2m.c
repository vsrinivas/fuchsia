// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <dev/interrupt/arm_gic.h>
#include <dev/interrupt/arm_gicv2m.h>
#include <dev/interrupt/arm_gic_regs.h>
#include <err.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

// Section 9.7
#define MSI_TYPER_OFFSET     (0x008)  // Type Register
#define MSI_SETSPI_NS_OFFSET (0x040)  // Doorbell register (whack here for interrupt)
#define MSI_IIDR_OFFSET      (0xFCC)  // Interface ID register
#define REG_RD(base, off)    (((volatile uint32_t*)(base))[(off) >> 2])

// Section 9.9.1
#define MIN_VALID_MSI_SPI (32)
#define MAX_VALID_MSI_SPI (1020)

const paddr_t* g_reg_frames;
const vaddr_t* g_reg_frames_virt;
uint           g_reg_frame_count;

void arm_gicv2m_init(const paddr_t* reg_frames, const vaddr_t* reg_frames_virt, const uint reg_frame_count) {
    // Protect against double init.
    DEBUG_ASSERT(!g_reg_frames);
    DEBUG_ASSERT(!g_reg_frame_count);

    // If the user has no register frames, they should be using arm_gic, not
    // arm_gicv2m
    DEBUG_ASSERT(reg_frames);
    DEBUG_ASSERT(reg_frame_count);

    // Stash the frame info
    g_reg_frames      = reg_frames;
    g_reg_frames_virt = reg_frames_virt;
    g_reg_frame_count = reg_frame_count;

    // Walk the list of regions, and make sure that all of the controlled SPIs
    // are configured for edge triggered mode.
    for (uint i = 0; i < g_reg_frame_count; ++i) {
        uint32_t type_reg = REG_RD(g_reg_frames_virt[i], MSI_TYPER_OFFSET);
        uint     base_spi = (type_reg >> 16) & 0x3FF;
        uint     num_spi  = type_reg & 0x3FF;

        for (uint i = 0; i < num_spi; ++i) {
            uint spi_id = base_spi + i;
            if ((spi_id < MIN_VALID_MSI_SPI) || (spi_id > MAX_VALID_MSI_SPI)) {
                TRACEF("Invalid SPI ID (%u) found in GICv2m register frame @%p\n",
                        spi_id, (void*)g_reg_frames[i]);
                continue;
            }

            uint     reg_ndx   = spi_id >> 4;
            uint     bit_shift = ((spi_id & 0xF) << 1) + 1;
            uint32_t reg_val   = GICREG(0, GICD_ICFGR(reg_ndx));
            reg_val |= (0x1u << bit_shift);
            GICREG(0, GICD_ICFGR(reg_ndx)) = reg_val;
        }
    }

}

status_t arm_gicv2m_get_frame_info(const uint frame_ndx, arm_gicv2m_frame_info_t* out_info) {
    if (!out_info)
        return MX_ERR_INVALID_ARGS;

    memset(out_info, 0, sizeof(*out_info));

    if (!g_reg_frames || !g_reg_frame_count)
        return MX_ERR_UNAVAILABLE;

    if (frame_ndx >= g_reg_frame_count)
        return MX_ERR_NOT_FOUND;

    uint32_t type_reg = REG_RD(g_reg_frames_virt[frame_ndx], MSI_TYPER_OFFSET);
    uint     base_spi = (type_reg >> 16) & 0x3FF;
    uint     num_spi  = type_reg & 0x3FF;
    uint     last_spi = base_spi + num_spi - 1;

    if (!num_spi ||
        (base_spi < MIN_VALID_MSI_SPI) ||
        (last_spi > MAX_VALID_MSI_SPI))
        return MX_ERR_BAD_STATE;

    out_info->start_spi_id = base_spi;
    out_info->end_spi_id   = last_spi;
    out_info->doorbell     = g_reg_frames[frame_ndx] + MSI_SETSPI_NS_OFFSET;
    out_info->iid          = REG_RD(g_reg_frames_virt[frame_ndx], MSI_IIDR_OFFSET);

    return MX_OK;
}
