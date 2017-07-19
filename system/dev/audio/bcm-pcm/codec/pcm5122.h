// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/audio.h>
#include <magenta/device/i2c.h>

#pragma once
// clang-format off
#define PCM5122_REG_PLL_ENABLE              (uint8_t)(4)

#define PCM5122_REG_GPIO_ENABLE             (uint8_t)(8)

#define PCM5122_REG_PLL_CLK_SOURCE          (uint8_t)(13)
#define PCM5122_PLL_CLK_SOURCE_SCK          (uint8_t)(0x00)
#define PCM5122_PLL_CLK_SOURCE_BCK          (uint8_t)(0x10)
#define PCM5122_PLL_CLK_SOURCE_GPIO         (uint8_t)(0x30)

#define PCM5122_REG_DAC_CLK_SOURCE          (uint8_t)(14)

#define PCM5122_REG_PLL_P                   (uint8_t)(20)
#define PCM5122_REG_PLL_J                   (uint8_t)(21)
#define PCM5122_REG_PLL_D_HI                (uint8_t)(22)
#define PCM5122_REG_PLL_D_LO                (uint8_t)(23)
#define PCM5122_REG_PLL_R                   (uint8_t)(24)

#define PCM5122_REG_DSP_CLK_DIVIDER         (uint8_t)(27)
#define PCM5122_REG_DAC_CLK_DIVIDER         (uint8_t)(28)
#define PCM5122_REG_NCP_CLK_DIVIDER         (uint8_t)(29)
#define PCM5122_REG_OSR_CLK_DIVIDER         (uint8_t)(30)
#define PCM5122_REG_FS_SPEED_MODE           (uint8_t)(34)

#define PCM5122_REG_ERROR_MASK              (uint8_t)(37)
#define PCM5122_REG_I2S_CONTROL             (uint8_t)(40)

#define PCM5122_REG_GPIO4_OUTPUT_SELECTION  (uint8_t)(83)
#define PCM5122_REG_GPIO_CONTROL            (uint8_t)(86)

// PCM5122 datasheet uses 1..6 for gpio names (*does not start at 0 :-| )
#define PCM5122_GPIO1                       (uint8_t)(0)
#define PCM5122_GPIO2                       (uint8_t)(1)
#define PCM5122_GPIO3                       (uint8_t)(2)
#define PCM5122_GPIO4                       (uint8_t)(3)
#define PCM5122_GPIO5                       (uint8_t)(4)
#define PCM5122_GPIO6                       (uint8_t)(5)

#define PCM5122_GPIO_HIGH                   (uint8_t)(1)
#define PCM5122_GPIO_LOW                    (uint8_t)(0)

#define PCM5122_GPIO_OUTPUT                 (uint8_t)(1)
#define PCM5122_GPIO_INPUT                  (uint8_t)(0)

// source selection for GPIO outputs
#define PCM5122_GPIO_SELECT_OFF             (uint8_t)(0x00)
#define PCM5122_GPIO_SELECT_DSP             (uint8_t)(0x01)
#define PCM5122_GPIO_SELECT_REG_OUT         (uint8_t)(0x02)
#define PCM5122_GPIO_SELECT_MUTE_FLAG_LR    (uint8_t)(0x03)
#define PCM5122_GPIO_SELECT_MUTE_FLAG_L     (uint8_t)(0x04)
#define PCM5122_GPIO_SELECT_MUTE_FLAG_R     (uint8_t)(0x05)
#define PCM5122_GPIO_SELECT_CLK_INVALID     (uint8_t)(0x06)
#define PCM5122_GPIO_SELECT_SDOUT           (uint8_t)(0x07)
#define PCM5122_GPIO_SELECT_ANA_MUTE_L      (uint8_t)(0x08)
#define PCM5122_GPIO_SELECT_ANA_MUTE_R      (uint8_t)(0x09)
#define PCM5122_GPIO_SELECT_PLL_LOCK        (uint8_t)(0x0a)
#define PCM5122_GPIO_SELECT_CP_CLOCK        (uint8_t)(0x0b)a
#define PCM5122_GPIO_SELECT_RES0            (uint8_t)(0x0c)
#define PCM5122_GPIO_SELECT_RES1            (uint8_t)(0x0d)
#define PCM5122_GPIO_SELECT_UNDER_V_0P7     (uint8_t)(0x0e)
#define PCM5122_GPIO_SELECT_UNDER_V_0P3     (uint8_t)(0x0f)
#define PCM5122_GPIO_SELECT_PLL_OUT         (uint8_t)(0x10)


#define HIFIBERRY_I2C_ADDRESS 0x4d
// clang-format on

static inline void pcm5122_add_slave(int fd) {
    i2c_ioctl_add_slave_args_t add_slave_args = {
        .chip_address_width = I2C_7BIT_ADDRESS,
        .chip_address = HIFIBERRY_I2C_ADDRESS,
    };

    ioctl_i2c_bus_add_slave(fd, &add_slave_args);
}

static inline void pcm5122_write_reg(int fd, uint8_t address, uint8_t value) {
    pcm5122_add_slave(fd);
    uint8_t argbuff[2] = {address, value};
    write(fd, argbuff, sizeof(argbuff));
}
