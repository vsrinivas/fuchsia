// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fcntl.h>
#include <magenta/device/i2c.h>
#include <magenta/syscalls.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "hifi-berry.h"
#include "pcm5122.h"

/*
    HiFiBerry DAC+ - i2s slave, i2c control mode, using BCLK as the reference

    To keep things simple/manageable, always assume a i2s interface with
     64bclk per audio frame

*/

#define HIFIBERRY_I2C_ADDRESS 0x4d
#define DEVNAME "/dev/soc/bcm-i2c/i2c1"

typedef struct {
    int i2c_fd;
    uint32_t state;
} hifiberry_t;

static hifiberry_t* hfb = NULL;

static mx_status_t hifiberry_LED_ctl(bool state) {

    if (!hfb)
        return MX_ERR_BAD_STATE;
    if (hfb->i2c_fd < 0)
        return MX_ERR_BAD_STATE;
    if (hfb->state == HIFIBERRY_STATE_SHUTDOWN)
        return MX_ERR_BAD_STATE;
    // Not using any other GPIO pins, so don't worry about state of other
    //  pins.
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_GPIO_CONTROL,
                     (state) ? (PCM5122_GPIO_HIGH << PCM5122_GPIO4) :
                               (PCM5122_GPIO_LOW  << PCM5122_GPIO4));

    return MX_OK;
}

mx_status_t hifiberry_release(void) {

    if (!hfb)
        return MX_OK;
    hifiberry_LED_ctl(false);

    if (hfb->i2c_fd >= 0) {
        close(hfb->i2c_fd);
    }

    free(hfb);
    hfb = NULL;

    return MX_OK;
}

mx_status_t hifiberry_start(void) {
    return hifiberry_LED_ctl(true);
}

mx_status_t hifiberry_stop(void) {
    return hifiberry_LED_ctl(false);
}

mx_status_t hifiberry_init(void) {

    // Check to see if already initialized
    if ((hfb) && (hfb->state != HIFIBERRY_STATE_SHUTDOWN))
        return MX_ERR_BAD_STATE;

    if (hfb == NULL) {
        hfb = calloc(1, sizeof(hifiberry_t));
        if (!hfb)
            return MX_ERR_NO_MEMORY;
    }

    hfb->i2c_fd = open(DEVNAME, O_RDWR);
    if (hfb->i2c_fd < 0) {
        printf("HIFIBERRY: Control channel not found\n");
        return MX_ERR_NOT_FOUND;
    }

    i2c_ioctl_add_slave_args_t add_slave_args = {
        .chip_address_width = I2C_7BIT_ADDRESS,
        .chip_address = HIFIBERRY_I2C_ADDRESS,
    };

    ssize_t ret = ioctl_i2c_bus_add_slave(hfb->i2c_fd, &add_slave_args);
    if (ret < 0) {
        return MX_ERR_INTERNAL;
    }
    // configure LED GPIO
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_GPIO_ENABLE,
                                   PCM5122_GPIO_OUTPUT << PCM5122_GPIO4);

    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_GPIO4_OUTPUT_SELECTION,
                                   PCM5122_GPIO_SELECT_REG_OUT );
    hifiberry_LED_ctl(false);

    // Clock source for pll = 1 (bclk)
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_PLL_CLK_SOURCE,
                                   PCM5122_PLL_CLK_SOURCE_BCK);

    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_ERROR_MASK, (1 << 4) | // Ignore sck detection
                                                           (1 << 3) | // Ignore sck halt detection
                                                           (1 << 2)); // Disable clock autoset

    // Most of the below are mode specific, should defer to some mode set routine...

    // DDSP divider 1 (=/2)
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_DSP_CLK_DIVIDER, 1);
    // DAC Divider = /16
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_DAC_CLK_DIVIDER, 15);
    // NCP Divider = /4
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_NCP_CLK_DIVIDER, 3);
    // OSR Divider = /8
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_OSR_CLK_DIVIDER, 7);
    // DAC CLK Mux = PLL
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_DAC_CLK_SOURCE, 0x10);
    // Enable the PLL
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_PLL_ENABLE, (1 << 0));

    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_PLL_P, 0);   // P = 0
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_PLL_J, 16);  // J = 16
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_PLL_D_HI, 0);// D = 0
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_PLL_D_LO, 0);// (D uses two registers)
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_PLL_R, 1);   // R = 2

    hfb->state |= HIFIBERRY_STATE_INITIALIZED;

    return MX_OK;
}

bool hifiberry_is_valid_mode(audio_stream_cmd_set_format_req_t req) {

    uint32_t mode = req.sample_format & (AUDIO_SAMPLE_FORMAT_16BIT);
    if (!mode)
        return false;

    if (req.channels != 2)
        return false;
    if ((req.frames_per_second != 48000) && (req.frames_per_second != 44100))
        return false;

    return true;
}
