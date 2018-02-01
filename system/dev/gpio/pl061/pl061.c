// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/reg.h>
#include <limits.h>
#include <stdio.h>

#include <gpio/pl061/pl061.h>

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

static zx_status_t pl061_gpio_config(void* ctx, uint32_t index, uint32_t flags) {
    pl061_gpios_t* gpios = ctx;
    index -= gpios->gpio_start;
    volatile uint8_t* regs = io_buffer_virt(&gpios->buffer) + PAGE_SIZE * (index / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (index % GPIOS_PER_PAGE);

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
    return ZX_OK;
}

static zx_status_t pl061_gpio_read(void* ctx, uint32_t index, uint8_t* out_value) {
    pl061_gpios_t* gpios = ctx;
    index -= gpios->gpio_start;
    volatile uint8_t* regs = io_buffer_virt(&gpios->buffer) + PAGE_SIZE * (index / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (index % GPIOS_PER_PAGE);

    *out_value = !!(readb(regs + GPIODATA(bit)) & bit);
    return ZX_OK;
}

static zx_status_t pl061_gpio_write(void* ctx, uint32_t index, uint8_t value) {
    pl061_gpios_t* gpios = ctx;
    index -= gpios->gpio_start;
    volatile uint8_t* regs = io_buffer_virt(&gpios->buffer) + PAGE_SIZE * (index / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (index % GPIOS_PER_PAGE);

    writeb((value ? bit : 0), regs + GPIODATA(bit));
    return ZX_OK;
}

gpio_protocol_ops_t pl061_proto_ops = {
    .config = pl061_gpio_config,
    .read = pl061_gpio_read,
    .write = pl061_gpio_write,
};
