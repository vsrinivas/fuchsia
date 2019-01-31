// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "a113-pdm.h"

#include <ddk/debug.h>
#include <unistd.h>

// Filter configuration
static const int lpf1[] = {
    0x000014, 0xffffb2, 0xfffed9, 0xfffdce, 0xfffd45, 0xfffe32, 0x000147,
    0x000645, 0x000b86, 0x000e21, 0x000ae3, 0x000000, 0xffeece, 0xffdca8,
    0xffd212, 0xffd7d1, 0xfff2a7, 0x001f4c, 0x0050c2, 0x0072aa, 0x006ff1,
    0x003c32, 0xffdc4e, 0xff6a18, 0xff0fef, 0xfefbaf, 0xff4c40, 0x000000,
    0x00ebc8, 0x01c077, 0x02209e, 0x01c1a4, 0x008e60, 0xfebe52, 0xfcd690,
    0xfb8fa5, 0xfba498, 0xfd9812, 0x0181ce, 0x06f5f3, 0x0d112f, 0x12a958,
    0x169686, 0x18000e, 0x169686, 0x12a958, 0x0d112f, 0x06f5f3, 0x0181ce,
    0xfd9812, 0xfba498, 0xfb8fa5, 0xfcd690, 0xfebe52, 0x008e60, 0x01c1a4,
    0x02209e, 0x01c077, 0x00ebc8, 0x000000, 0xff4c40, 0xfefbaf, 0xff0fef,
    0xff6a18, 0xffdc4e, 0x003c32, 0x006ff1, 0x0072aa, 0x0050c2, 0x001f4c,
    0xfff2a7, 0xffd7d1, 0xffd212, 0xffdca8, 0xffeece, 0x000000, 0x000ae3,
    0x000e21, 0x000b86, 0x000645, 0x000147, 0xfffe32, 0xfffd45, 0xfffdce,
    0xfffed9, 0xffffb2, 0x000014,
};

static const int lpf3[] = {
    0x000000, 0x000081, 0x000000, 0xfffedb, 0x000000, 0x00022d, 0x000000,
    0xfffc46, 0x000000, 0x0005f7, 0x000000, 0xfff6eb, 0x000000, 0x000d4e,
    0x000000, 0xffed1e, 0x000000, 0x001a1c, 0x000000, 0xffdcb0, 0x000000,
    0x002ede, 0x000000, 0xffc2d1, 0x000000, 0x004ebe, 0x000000, 0xff9beb,
    0x000000, 0x007dd7, 0x000000, 0xff633a, 0x000000, 0x00c1d2, 0x000000,
    0xff11d5, 0x000000, 0x012368, 0x000000, 0xfe9c45, 0x000000, 0x01b252,
    0x000000, 0xfdebf6, 0x000000, 0x0290b8, 0x000000, 0xfcca0d, 0x000000,
    0x041d7c, 0x000000, 0xfa8152, 0x000000, 0x07e9c6, 0x000000, 0xf28fb5,
    0x000000, 0x28b216, 0x3fffde, 0x28b216, 0x000000, 0xf28fb5, 0x000000,
    0x07e9c6, 0x000000, 0xfa8152, 0x000000, 0x041d7c, 0x000000, 0xfcca0d,
    0x000000, 0x0290b8, 0x000000, 0xfdebf6, 0x000000, 0x01b252, 0x000000,
    0xfe9c45, 0x000000, 0x012368, 0x000000, 0xff11d5, 0x000000, 0x00c1d2,
    0x000000, 0xff633a, 0x000000, 0x007dd7, 0x000000, 0xff9beb, 0x000000,
    0x004ebe, 0x000000, 0xffc2d1, 0x000000, 0x002ede, 0x000000, 0xffdcb0,
    0x000000, 0x001a1c, 0x000000, 0xffed1e, 0x000000, 0x000d4e, 0x000000,
    0xfff6eb, 0x000000, 0x0005f7, 0x000000, 0xfffc46, 0x000000, 0x00022d,
    0x000000, 0xfffedb, 0x000000, 0x000081, 0x000000,
};

static const int lpf2[] = {
    0x00050a, 0xfff004, 0x0002c1, 0x003c12, 0xffa818, 0xffc87d, 0x010aef,
    0xff5223, 0xfebd93, 0x028f41, 0xff5c0e, 0xfc63f8, 0x055f81, 0x000000,
    0xf478a0, 0x11c5e3, 0x2ea74d, 0x11c5e3, 0xf478a0, 0x000000, 0x055f81,
    0xfc63f8, 0xff5c0e, 0x028f41, 0xfebd93, 0xff5223, 0x010aef, 0xffc87d,
    0xffa818, 0x003c12, 0x0002c1, 0xfff004, 0x00050a,
};

void a113_pdm_enable(a113_audio_device_t* audio_device, int is_enable) {
    if (is_enable) {
        a113_pdm_update_bits(audio_device, PDM_CTRL, 1 << 31, is_enable << 31);
    } else {
        a113_pdm_update_bits(audio_device, PDM_CTRL, 1 << 31 | 1 << 16,
                             0 << 31 | 0 << 16);
        // Amlogic recommend a sleep after pdm_disable. Not sure why yet. In
        // our code structure the sleep doesn't actually do anything since all
        // the code is async. Disable it for now, and revisit if we actually
        // end up seeing issues here.
        // zx_nanosleep(1);
    }
}

void a113_pdm_fifo_reset(a113_audio_device_t* audio_device) {
    // Toggle this bit for fifo reset.
    a113_pdm_update_bits(audio_device, PDM_CTRL, 1 << 16, 0 << 16);
    a113_pdm_update_bits(audio_device, PDM_CTRL, 1 << 16, 1 << 16);
}

void a113_pdm_ctrl(a113_audio_device_t* audio_device, int bitdepth) {
    a113_pdm_write(audio_device, PDM_CLKG_CTRL, 1);

    uint32_t mode = bitdepth == 32 ? 0 : 1;
    a113_pdm_update_bits(
        audio_device, PDM_CTRL, (0x7 << 28 | 0xff << 8 | 0xff << 0),
        (0 << 30) | (mode << 29) | (0 << 28) | (0xff << 8) | (0xff << 0));

    a113_pdm_write(audio_device, PDM_CHAN_CTRL,
                   ((28 << 24) | (28 << 16) | (28 << 8) | (28 << 0)));
    a113_pdm_write(audio_device, PDM_CHAN_CTRL1,
                   ((28 << 24) | (28 << 16) | (28 << 8) | (28 << 0)));
}

void a113_pdm_arb_config(a113_audio_device_t* aml_tdm_dev) {
    a113_ee_audio_write(aml_tdm_dev, EE_AUDIO_ARB_CTRL, 1 << 31 | 0xff << 0);
}

static void a113_pdm_filters_config(a113_audio_device_t* audio_device,
                                    int lpf1_len, int lpf2_len, int lpf3_len) {}

static void a113_pdm_LPF_coeff(a113_audio_device_t* audio_device, int lpf1_len,
                               const int* lpf1_coeff, int lpf2_len,
                               const int* lpf2_coeff, int lpf3_len,
                               const int* lpf3_coeff) {}

void a113_pdm_filter_ctrl(a113_audio_device_t* audio_device) {

    a113_pdm_write(
        audio_device, PDM_HCIC_CTRL1,
        (0x80000000 | 0x7 | (0x8 << 4) | (0x80 << 16) | (0x11 << 24)));

    a113_pdm_write(audio_device, PDM_F1_CTRL,
                   (0x80000000 | 87 | (2 << 12) | (1 << 16)));
    a113_pdm_write(audio_device, PDM_F2_CTRL,
                   (0x80000000 | 33 | (2 << 12) | (0 << 16)));
    a113_pdm_write(audio_device, PDM_F3_CTRL,
                   (0x80000000 | 117 | (2 << 12) | (1 << 16)));

    a113_pdm_write(audio_device, PDM_HPF_CTRL,
                   (0x8000 | (0x7 << 16) | (1 << 31)));

    a113_pdm_write(audio_device, PDM_COEFF_ADDR, 0);

    for (int i = 0; i < 87; i++)
        a113_pdm_write(audio_device, PDM_COEFF_DATA, lpf1[i]);
    for (int i = 0; i < 33; i++)
        a113_pdm_write(audio_device, PDM_COEFF_DATA, lpf2[i]);
    for (int i = 0; i < 117; i++)
        a113_pdm_write(audio_device, PDM_COEFF_DATA, lpf3[i]);

    a113_pdm_write(audio_device, PDM_COEFF_ADDR, 0);
}
