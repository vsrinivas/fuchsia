// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "a113-ddr.h"

#include "a113-audio-device.h"

void a113_audio_register_toddr(a113_audio_device_t* audio_device) {
    // For now, we only use DDR B for PDM in. For other inputs, we will needs
    // other TODDRs.
    uint32_t id = 1;

    // Enable audio ddr arb
    a113_ee_audio_update_bits(audio_device, EE_AUDIO_ARB_CTRL,
                              1 << 31 | 1 << id, 1 << 31 | 1 << id);
}

void a113_audio_unregister_toddr(a113_audio_device_t* audio_device) {
    uint32_t mask_bit = 1; // for now, we only use TODDR_B.
    uint32_t value;

    a113_ee_audio_update_bits(audio_device, EE_AUDIO_ARB_CTRL, 1 << mask_bit,
                              0 << mask_bit);

    // No ddr active, disable arb switch
    value = a113_ee_audio_read(audio_device, EE_AUDIO_ARB_CTRL) & 0x77;
    if (value == 0)
        a113_ee_audio_update_bits(audio_device, EE_AUDIO_ARB_CTRL, 1 << 31,
                                  0 << 31);
}

void a113_toddr_set_buf(a113_audio_device_t* audio_device, uint32_t start,
                        uint32_t end) {
    a113_ee_audio_write(audio_device, EE_AUDIO_TODDR_B_START_ADDR, start);
    a113_ee_audio_write(audio_device, EE_AUDIO_TODDR_B_FINISH_ADDR, end);
}

void a113_toddr_set_intrpt(a113_audio_device_t* audio_device, uint32_t intrpt) {
    a113_ee_audio_write(audio_device, EE_AUDIO_TODDR_B_INT_ADDR, intrpt);
    a113_ee_audio_update_bits(audio_device, EE_AUDIO_TODDR_B_CTRL0, 0xff << 16,
                              1 << 18);
}

uint32_t a113_toddr_get_position(a113_audio_device_t* audio_device) {
    return a113_ee_audio_read(audio_device, EE_AUDIO_TODDR_B_STATUS2);
}

uint32_t a113_toddr_get_int_status(a113_audio_device_t* audio_device) {

    return a113_ee_audio_read(audio_device, EE_AUDIO_TODDR_B_STATUS1) & 0xff;
}

void a113_toddr_clear_interrupt(a113_audio_device_t* audio_device,
                                uint32_t interrupt_mask) {
    a113_ee_audio_update_bits(audio_device, EE_AUDIO_TODDR_B_CTRL1,
                              interrupt_mask, 0xff);
    a113_ee_audio_update_bits(audio_device, EE_AUDIO_TODDR_B_CTRL1,
                              interrupt_mask, 0x0);
}

void a113_toddr_enable(a113_audio_device_t* audio_device, bool enable) {

    a113_ee_audio_update_bits(audio_device, EE_AUDIO_TODDR_B_CTRL0, 1 << 31,
                              enable << 31);
}

void a113_toddr_select_src(a113_audio_device_t* audio_device,
                           enum toddr_src src) {

    a113_ee_audio_update_bits(audio_device, EE_AUDIO_TODDR_B_CTRL0, 0x7,
                              src & 0x7);
}

void a113_toddr_set_fifos(a113_audio_device_t* audio_device, uint32_t thresh) {

    a113_ee_audio_write(audio_device, EE_AUDIO_TODDR_B_CTRL1,
                        (thresh - 1) << 16 | 2 << 8);
}

void a113_toddr_set_format(a113_audio_device_t* audio_device, uint32_t type,
                           uint32_t msb, uint32_t lsb) {

    a113_ee_audio_update_bits(audio_device, EE_AUDIO_TODDR_B_CTRL0, 0x1fff << 3,
                              type << 13 | msb << 8 | lsb << 3);
}
