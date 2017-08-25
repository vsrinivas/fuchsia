// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/reg.h>
#include <limits.h>
#include <stdio.h>

#include "pl061.h"

// GPIO register offsets
#define GPIODATA(mask)  ((mask) << 2)   // Data registers, mask provided as index
#define GPIODIR         0x400           // Data direction register (0 = IN, 1 = OUT)
#define GPIOIS          0x404           // Interrupt sense register (0 = edge, 1 = level)
#define GPIOIBE         0x408           // Interrupt both edges register (1 = both)
#define GPIOIEV         0x40C           // Interrupt event register (0 = falling, 1 = rising)
#define GPIOIE          0x410           // Interrupt mask register (1 = interrupt masked)
#define GPIORIS         0x414           // Raw interrupt status register
#define GPIOMIS         0x418           // Masked interrupt status register
#define GPIOIC          0x41C           // Interrupt clear register
#define GPIOAFSEL       0x420           // Mode control select register

#define GPIOS_PER_PAGE  8

static mx_status_t pl061_gpio_config(void* ctx, unsigned pin, gpio_config_flags_t flags) {
    pl061_gpios_t* gpios = ctx;
    pin -= gpios->gpio_start;
    volatile uint8_t* regs = gpios->buffer.vaddr + PAGE_SIZE * (pin / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (pin % GPIOS_PER_PAGE);

    mtx_lock(&gpios->lock);
    uint8_t dir = readb(regs + GPIODIR);
    if ((flags & GPIO_DIR_MASK) == GPIO_DIR_OUT) {
        dir |= bit;
    } else {
        dir &= ~bit;
    }
    writeb(dir, regs + GPIODIR);

    uint8_t trigger = readb(regs + GPIOIS);
    if ((flags & GPIO_TRIGGER_MASK) == GPIO_TRIGGER_LEVEL) {
        trigger |= bit;
    } else {
        trigger &= ~bit;
    }
    writeb(trigger, regs + GPIOIS);

    uint8_t be = readb(regs + GPIOIBE);
    uint8_t iev = readb(regs + GPIOIEV);

    if ((flags & GPIO_TRIGGER_MASK) == GPIO_TRIGGER_EDGE && (flags & GPIO_TRIGGER_RISING)
        && (flags & GPIO_TRIGGER_FALLING)) {
        be |= bit;
     } else {
        be &= ~bit;
     }
    if ((flags & GPIO_TRIGGER_MASK) == GPIO_TRIGGER_EDGE && (flags & GPIO_TRIGGER_RISING)
        && !(flags & GPIO_TRIGGER_FALLING)) {
        iev |= bit;
     } else {
        iev &= ~bit;
     }

    writeb(be, regs + GPIOIBE);
    writeb(iev, regs + GPIOIEV);

    mtx_unlock(&gpios->lock);
    return MX_OK;
}

static mx_status_t pl061_gpio_read(void* ctx, unsigned pin, unsigned* out_value) {
    pl061_gpios_t* gpios = ctx;
    pin -= gpios->gpio_start;
    volatile uint8_t* regs = gpios->buffer.vaddr + PAGE_SIZE * (pin / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (pin % GPIOS_PER_PAGE);

    *out_value = !!(readb(regs + GPIODATA(0)) & bit);
    return MX_OK;
}

static mx_status_t pl061_gpio_write(void* ctx, unsigned pin, unsigned value) {
    pl061_gpios_t* gpios = ctx;
    pin -= gpios->gpio_start;
    volatile uint8_t* regs = gpios->buffer.vaddr + PAGE_SIZE * (pin / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (pin % GPIOS_PER_PAGE);

    writeb((value ? bit : 0), regs + GPIODATA(bit));
    return MX_OK;
}

static mx_status_t pl061_gpio_int_enable(void* ctx, unsigned pin, bool enable) {
    pl061_gpios_t* gpios = ctx;
    pin -= gpios->gpio_start;
    volatile uint8_t* regs = gpios->buffer.vaddr + PAGE_SIZE * (pin / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (pin % GPIOS_PER_PAGE);

    mtx_lock(&gpios->lock);
    uint8_t ie = readb(regs + GPIOIE);
    if (enable) {
        ie |= bit;
    } else {
        ie &= ~bit;
    }
    writeb(ie, regs + GPIOIE);
    mtx_unlock(&gpios->lock);
    return MX_OK;
}

static mx_status_t pl061_gpio_get_int_status(void* ctx, unsigned pin, bool* out_status) {
    pl061_gpios_t* gpios = ctx;
    pin -= gpios->gpio_start;
    volatile uint8_t* regs = gpios->buffer.vaddr + PAGE_SIZE * (pin / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (pin % GPIOS_PER_PAGE);

    *out_status = !!(readb(regs + GPIOMIS) & bit);
    return MX_OK;
}

static mx_status_t pl061_gpio_int_clear(void* ctx, unsigned pin) {
    pl061_gpios_t* gpios = ctx;
    pin -= gpios->gpio_start;
    volatile uint8_t* regs = gpios->buffer.vaddr + PAGE_SIZE * (pin / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (pin % GPIOS_PER_PAGE);

    mtx_lock(&gpios->lock);
    uint8_t ic = readb(regs + GPIOIC);
    ic |= bit;
    writeb(ic, regs + GPIOIC);
    mtx_unlock(&gpios->lock);
    return MX_OK;
}

gpio_protocol_ops_t pl061_proto_ops = {
    .config = pl061_gpio_config,
    .read = pl061_gpio_read,
    .write = pl061_gpio_write,
    .int_enable = pl061_gpio_int_enable,
    .get_int_status = pl061_gpio_get_int_status,
    .int_clear = pl061_gpio_int_clear,
};
