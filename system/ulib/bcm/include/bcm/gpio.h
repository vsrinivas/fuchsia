// Copyright 2017 The Fuchsia Authors
// All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define BCM_GPIO0_PIN   (0)
#define BCM_GPIO1_PIN   (1)
#define BCM_GPIO2_PIN   (2)
#define BCM_GPIO3_PIN   (3)

#define BCM_GPIO18_PIN      (18)
#define BCM_GPIO19_PIN      (19)
#define BCM_GPIO20_PIN      (20)
#define BCM_GPIO21_PIN      (21)

#define BCM_SDA0_PIN    BCM_GPIO0_PIN
#define BCM_SCL0_PIN    BCM_GPIO1_PIN
#define BCM_SDA1_PIN    BCM_GPIO2_PIN
#define BCM_SCL1_PIN    BCM_GPIO3_PIN

#define BCM_PCM_CLK_ALT0_PIN    BCM_GPIO18_PIN
#define BCM_PCM_FS_ALT0_PIN     BCM_GPIO19_PIN
#define BCM_PCM_DIN_ALT0_PIN    BCM_GPIO20_PIN
#define BCM_PCM_DOUT_ALT0_PIN   BCM_GPIO21_PIN

#define BCM_GPIO_GPFSEL_MASK    (0x07)

enum gpio_fsel_t {

    FSEL_INPUT      = 0x000,
    FSEL_OUTPUT     = 0x001,
    FSEL_ALT0       = 0x004,
    FSEL_ALT1       = 0x005,
    FSEL_ALT2       = 0x006,
    FSEL_ALT3       = 0x007,
    FSEL_ALT4       = 0x003,
    FSEL_ALT5       = 0x002,
};

typedef volatile struct {

    uint32_t    GPFSEL0;
    uint32_t    GPFSEL1;
    uint32_t    GPFSEL2;
    uint32_t    GPFSEL3;
    uint32_t    GPFSEL4;
    uint32_t    GPFSEL5;
    uint32_t    RES0;

    uint32_t    GPSET0;
    uint32_t    GPSET1;
    uint32_t    RES1;

    uint32_t    GPCLR0;
    uint32_t    GPCLR1;
    uint32_t    RES2;

    uint32_t    GPLEV0;
    uint32_t    GPLEV1;
    uint32_t    RES3;

    uint32_t    GPEDS0;
    uint32_t    GPEDS1;
    uint32_t    RES4;

    uint32_t    GPREN0;
    uint32_t    GPREN1;
    uint32_t    RES5;

    uint32_t    GPFEN0;
    uint32_t    GPFEN1;
    uint32_t    RES6;

    uint32_t    GPHEN0;
    uint32_t    GPHEN1;
    uint32_t    RES7;

    uint32_t    GPLEN0;
    uint32_t    GPLEN1;
    uint32_t    RES8;

    uint32_t    GPAREN0;
    uint32_t    GPAREN1;
    uint32_t    RES9;

    uint32_t    GPAFEN0;
    uint32_t    GPAFEN1;
    uint32_t    RES10;

    uint32_t    GPPUD;
    uint32_t    GPPUDCLK0;
    uint32_t    GPPUDCLK1;
} bcm_gpio_ctrl_t;


static inline void set_gpio_function(bcm_gpio_ctrl_t* gpio, uint32_t pin, enum gpio_fsel_t fsel) {

    if (pin > 53) return;

    volatile uint32_t* gpfsel = &(gpio->GPFSEL0);

    gpfsel[ pin / 10 ] = (gpfsel[ pin / 10 ] & ~( (uint32_t)(((1 << 3) -1) << (3 * (pin % 10))))) |
                                    (fsel << (3 * (pin % 10)));

}

static inline void gpio_pin_set(bcm_gpio_ctrl_t* gpio, uint32_t pin) {

    if (pin > 53) return;

    volatile uint32_t* gpset = &(gpio->GPSET0);
    gpset[ pin / 32 ] = (uint32_t)(1 << (pin % 32));

}

static inline void gpio_pin_clr(bcm_gpio_ctrl_t* gpio, uint32_t pin) {

    if (pin > 53) return;

    volatile uint32_t* gpset = &(gpio->GPCLR0);
    gpset[ pin / 32 ] = (uint32_t)(1 << (pin % 32));

}
