// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include "a113-audio-device.h"

#define REGDUMPEEAUDIO(regval)          \
    zxlogf(INFO, #regval " = 0x%08x\n", \
           a113_ee_audio_read(audio_device, regval));

#define REGDUMPPDM(regval)              \
    zxlogf(INFO, #regval " = 0x%08x\n", \
           a113_ee_audio_read(audio_device, regval));

void a113_pdm_dump_registers(a113_audio_device_t* audio_device) {
    REGDUMPPDM(PDM_CTRL)
    REGDUMPPDM(PDM_HCIC_CTRL1)
    REGDUMPPDM(PDM_HCIC_CTRL2)
    REGDUMPPDM(PDM_F1_CTRL)
    REGDUMPPDM(PDM_F2_CTRL)
    REGDUMPPDM(PDM_F3_CTRL)
    REGDUMPPDM(PDM_HPF_CTRL)
    REGDUMPPDM(PDM_CHAN_CTRL)
    REGDUMPPDM(PDM_CHAN_CTRL1)
    REGDUMPPDM(PDM_COEFF_ADDR)
    REGDUMPPDM(PDM_COEFF_DATA)
    REGDUMPPDM(PDM_CLKG_CTRL)
    REGDUMPPDM(PDM_STS)

    REGDUMPEEAUDIO(EE_AUDIO_CLK_GATE_EN)
    REGDUMPEEAUDIO(EE_AUDIO_CLK_PDMIN_CTRL0)
    REGDUMPEEAUDIO(EE_AUDIO_CLK_PDMIN_CTRL1)

    REGDUMPEEAUDIO(EE_AUDIO_TODDR_B_CTRL0)
    REGDUMPEEAUDIO(EE_AUDIO_TODDR_B_CTRL1)
    REGDUMPEEAUDIO(EE_AUDIO_TODDR_B_START_ADDR)
    REGDUMPEEAUDIO(EE_AUDIO_TODDR_B_FINISH_ADDR)
    REGDUMPEEAUDIO(EE_AUDIO_TODDR_B_INT_ADDR)
    REGDUMPEEAUDIO(EE_AUDIO_TODDR_B_STATUS1)
    REGDUMPEEAUDIO(EE_AUDIO_TODDR_B_STATUS2)
    REGDUMPEEAUDIO(EE_AUDIO_TODDR_B_START_ADDRB)
    REGDUMPEEAUDIO(EE_AUDIO_TODDR_B_FINISH_ADDRB)
}

uint32_t a113_pdm_read(a113_audio_device_t* audio_device, uint32_t reg) {
    return ((volatile uint32_t*)io_buffer_virt(&audio_device->pdm_mmio))[reg];
}

uint32_t a113_pdm_write(a113_audio_device_t* audio_device, uint32_t reg,
                        uint32_t value) {
    return ((volatile uint32_t*)io_buffer_virt(&audio_device->pdm_mmio))[reg] = value;
}

void a113_pdm_update_bits(a113_audio_device_t* audio_device, uint32_t reg,
                          uint32_t mask, uint32_t value) {
    uint32_t register_value = ((uint32_t*)io_buffer_virt(&audio_device->pdm_mmio))[reg];
    register_value &= ~mask;
    register_value |= value & mask;
    ((volatile uint32_t*)io_buffer_virt(&audio_device->pdm_mmio))[reg] = register_value;
}

uint32_t a113_ee_audio_read(a113_audio_device_t* audio_device, uint32_t reg) {
    return ((volatile uint32_t*)io_buffer_virt(&audio_device->ee_audio_mmio))[reg];
}

uint32_t a113_ee_audio_write(a113_audio_device_t* audio_device, uint32_t reg,
                             uint32_t value) {
    return ((volatile uint32_t*)io_buffer_virt(&audio_device->ee_audio_mmio))[reg] = value;
}

void a113_ee_audio_update_bits(a113_audio_device_t* audio_device, uint32_t reg,
                               uint32_t mask, uint32_t value) {
    volatile uint32_t register_value =
        ((volatile uint32_t*)io_buffer_virt(&audio_device->ee_audio_mmio))[reg];
    register_value &= ~mask;
    register_value |= value & mask;
    ((volatile uint32_t*)io_buffer_virt(&audio_device->ee_audio_mmio))[reg] = register_value;
}

// Map registers to our address space for future access, and do some very basic
// hardware initialization such as setting clocks.
zx_status_t a113_audio_device_init(a113_audio_device_t* audio_device,
                                   zx_device_t* parent) {
    ZX_DEBUG_ASSERT(audio_device);
    ZX_DEBUG_ASSERT(parent);

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV,
                                             &audio_device->pdev);
    if (status != ZX_OK) {
        goto init_fail;
    }

    // Map EE_AUDIO registers to our address space.
    status = pdev_map_mmio_buffer(&audio_device->pdev, 0 /* EE_AUDIO */,
                                  ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &audio_device->ee_audio_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "a113_audio_device_init: pdev_map_mmio_buffer failed\n");
        goto init_fail;
    }

    // Set clocks. This is done before mapping the PDM registers to our address
    // space. The PDM register is not accessible before the pdm_sysclk is
    // running.
    a113_ee_audio_write(audio_device, EE_AUDIO_CLK_PDMIN_CTRL0,
                        (1 << 31) | (2 << 24) | 79);
    a113_ee_audio_write(audio_device, EE_AUDIO_CLK_PDMIN_CTRL1,
                        (1 << 31) | (2 << 24) | 0);
    a113_ee_audio_write(audio_device, EE_AUDIO_CLK_GATE_EN, 0x000fffff);

    // Map the PDM registers to our address space.
    status = pdev_map_mmio_buffer(&audio_device->pdev, 1 /* PDM */,
                                  ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &audio_device->pdm_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "a113_audio_device_init: pdev_map_mmio_buffer failed\n");
        goto init_fail;
    }

    return ZX_OK;

init_fail:
    if (audio_device) {
        io_buffer_release(&audio_device->ee_audio_mmio);
        io_buffer_release(&audio_device->pdm_mmio);
    };
    return status;
}
