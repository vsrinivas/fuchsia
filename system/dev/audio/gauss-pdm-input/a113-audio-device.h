// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Thin layer that writes/reads to audio registers in the EE_AUDIO block and
// the PDM block.

#pragma once

__BEGIN_CDECLS;

#include <ddk/device.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include "a113-audio-regs.h"

typedef struct a113_audio_device {
    platform_device_protocol_t pdev;
    io_buffer_t ee_audio_mmio;
    io_buffer_t pdm_mmio;

    zx_handle_t pdm_irq;
    zx_handle_t bti;
} a113_audio_device_t;

zx_status_t a113_audio_device_init(a113_audio_device_t* audio_device,
                                   zx_device_t* parent);

uint32_t a113_ee_audio_read(a113_audio_device_t* audio_device, uint32_t reg);
uint32_t a113_ee_audio_write(a113_audio_device_t* audio_device, uint32_t reg,
                             uint32_t value);
void a113_ee_audio_update_bits(a113_audio_device_t* audio_device, uint32_t reg,
                               uint32_t mask, uint32_t value);

uint32_t a113_pdm_read(a113_audio_device_t* audio_device, uint32_t reg);
uint32_t a113_pdm_write(a113_audio_device_t* audio_device, uint32_t reg,
                        uint32_t val);
void a113_pdm_update_bits(a113_audio_device_t* audio_device, uint32_t reg,
                          uint32_t mask, uint32_t val);
__END_CDECLS;
