// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include "a113-audio-device.h"

__BEGIN_CDECLS;

// DDR types. From A113G datasheet.
enum ddr_types {
    LJ_8BITS,
    LJ_16BITS,
    RJ_16BITS,
    LJ_32BITS,
    RJ_32BITS,
};

// TODDR sources. From A113G datasheet.
enum toddr_src {
    TDMIN_A,
    TDMIN_B,
    TDMIN_C,
    SPDIFIN,
    PDMIN,
    NONE,
    TDMIN_LB,
    LOOPBACK,
};

void a113_audio_register_toddr(a113_audio_device_t* audio_device);
void a113_audio_unregister_toddr(a113_audio_device_t* audio_device);

void a113_toddr_set_buf(a113_audio_device_t* audio_device, uint32_t start,
                        uint32_t end);
void a113_toddr_set_intrpt(a113_audio_device_t* audio_device, uint32_t intrpt);
void a113_toddr_enable(a113_audio_device_t* audio_device, bool enable);
void a113_toddr_select_src(a113_audio_device_t* audio_device,
                           enum toddr_src src);
void a113_toddr_set_fifos(a113_audio_device_t* audio_device, uint32_t thresh);
void a113_toddr_set_format(a113_audio_device_t* audio_device, uint32_t type,
                           uint32_t msb, uint32_t lsb);

uint32_t a113_toddr_get_position(a113_audio_device_t* audio_device);
uint32_t a113_toddr_get_int_status(a113_audio_device_t* audio_device);

void a113_toddr_clear_interrupt(a113_audio_device_t* audio_device,
                                uint32_t interrupt_mask);

__END_CDECLS;
